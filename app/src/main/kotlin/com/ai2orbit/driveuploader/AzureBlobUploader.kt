package com.ai2orbit.driveuploader

import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.*
import kotlinx.coroutines.sync.Semaphore
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.*
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

data class AzureConfig(
    val accountName: String,
    val accountKey: String,
    val container: String = "ai2orbit-photos"
)

class AzureBlobUploader(
    private val config: AzureConfig,
    private val contentResolver: ContentResolver,
    private val profile: DeviceProfile
) {
    private val client = OkHttpClient.Builder()
        .retryOnConnectionFailure(true)
        .build()

    suspend fun uploadPhotos(
        uris: List<Uri>,
        onProgress: (current: Int, total: Int, result: UploadResult?) -> Unit
    ): UploadBatchResult = withContext(Dispatchers.IO) {
        val streamCount = profile.virtualStreams.coerceIn(7, 256)
        val semaphore = Semaphore(streamCount)

        ensureContainer()

        val jobs = uris.mapIndexed { index, uri ->
            async {
                semaphore.acquire()
                try {
                    val result = uploadSingle(uri)
                    withContext(Dispatchers.Main) {
                        onProgress(index + 1, uris.size, result)
                    }
                    result
                } finally {
                    semaphore.release()
                }
            }
        }

        val allResults = jobs.awaitAll()
        val successResults = allResults.filter { it.success }
        val totalBytes = allResults.sumOf { it.sizeBytes }
        val totalDuration = allResults.maxOfOrNull { it.durationMs } ?: 0L
        val avgSpeed = if (totalDuration > 0) {
            (totalBytes.toDouble() / 1024.0) / (totalDuration.toDouble() / 1000.0) * 8.0
        } else 0.0

        UploadBatchResult(
            results = allResults,
            totalFiles = allResults.size,
            successCount = successResults.size,
            failCount = allResults.size - successResults.size,
            totalBytes = totalBytes,
            totalDurationMs = totalDuration,
            avgSpeedKbps = avgSpeed
        )
    }

    private fun ensureContainer() {
        val url = "https://${config.accountName}.blob.core.windows.net/${config.container}?restype=container"
        val date = rfc1123Date()
        val stringToSign = "PUT\n\n\n0\n\n\n\n\n\n\n\n\nx-ms-date:$date\nx-ms-version:2020-10-02\n/${config.accountName}/${config.container}\nrestype:container"
        val signature = sign(stringToSign)

        val request = Request.Builder()
            .url(url)
            .put("".toRequestBody(null))
            .header("x-ms-date", date)
            .header("x-ms-version", "2020-10-02")
            .header("Content-Length", "0")
            .header("Authorization", "SharedKey ${config.accountName}:$signature")
            .build()

        try {
            client.newCall(request).execute().close()
        } catch (_: Exception) {}
    }

    private fun uploadSingle(uri: Uri): UploadResult {
        val fileName = getFileName(uri)
        val sizeBytes = getFileSize(uri)
        val start = System.currentTimeMillis()

        try {
            val inputStream = contentResolver.openInputStream(uri)
                ?: throw Exception("Cannot open file")
            val bytes = inputStream.readBytes()
            inputStream.close()

            val mimeType = contentResolver.getType(uri) ?: "image/jpeg"
            val blobName = fileName
            val url = "https://${config.accountName}.blob.core.windows.net/${config.container}/$blobName"

            val date = rfc1123Date()
            val contentLength = bytes.size.toString()

            val stringToSign = "PUT\n\n\n$contentLength\n\n$mimeType\n\n\n\n\n\n\n" +
                    "x-ms-blob-type:BlockBlob\n" +
                    "x-ms-date:$date\n" +
                    "x-ms-version:2020-10-02\n" +
                    "/${config.accountName}/${config.container}/$blobName"

            val signature = sign(stringToSign)

            val request = Request.Builder()
                .url(url)
                .put(bytes.toRequestBody(mimeType.toMediaType()))
                .header("x-ms-blob-type", "BlockBlob")
                .header("x-ms-date", date)
                .header("x-ms-version", "2020-10-02")
                .header("Content-Type", mimeType)
                .header("Content-Length", contentLength)
                .header("Authorization", "SharedKey ${config.accountName}:$signature")
                .build()

            val response = client.newCall(request).execute()
            val duration = System.currentTimeMillis() - start
            val speedKbps = if (duration > 0) {
                (sizeBytes.toDouble() / 1024.0) / (duration.toDouble() / 1000.0) * 8.0
            } else 0.0

            if (response.isSuccessful || response.code == 201) {
                return UploadResult(uri, blobName, fileName, sizeBytes, duration, speedKbps, true)
            } else {
                return UploadResult(uri, null, fileName, sizeBytes, duration, 0.0, false,
                    "Azure ${response.code}: ${response.message}")
            }
        } catch (e: Exception) {
            val duration = System.currentTimeMillis() - start
            return UploadResult(uri, null, fileName, sizeBytes, duration, 0.0, false, e.message)
        }
    }

    private fun sign(stringToSign: String): String {
        val keyBytes = android.util.Base64.decode(config.accountKey, android.util.Base64.DEFAULT)
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(keyBytes, "HmacSHA256"))
        val signed = mac.doFinal(stringToSign.toByteArray(Charsets.UTF_8))
        return android.util.Base64.encodeToString(signed, android.util.Base64.NO_WRAP)
    }

    private fun rfc1123Date(): String {
        val fmt = SimpleDateFormat("EEE, dd MMM yyyy HH:mm:ss 'GMT'", Locale.US)
        fmt.timeZone = TimeZone.getTimeZone("GMT")
        return fmt.format(Date())
    }

    private fun getFileName(uri: Uri): String {
        var name = "photo_${System.currentTimeMillis()}.jpg"
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (idx >= 0 && cursor.moveToFirst()) {
                name = cursor.getString(idx) ?: name
            }
        }
        return name
    }

    private fun getFileSize(uri: Uri): Long {
        var size = 0L
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val idx = cursor.getColumnIndex(OpenableColumns.SIZE)
            if (idx >= 0 && cursor.moveToFirst()) {
                size = cursor.getLong(idx)
            }
        }
        return size
    }
}
