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

data class S3Config(
    val accessKey: String,
    val secretKey: String,
    val bucket: String,
    val region: String = "us-east-1",
    val prefix: String = "AI2ORBIT_Photos/"
)

class S3Uploader(
    private val config: S3Config,
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
            val key = config.prefix + fileName

            val host = "${config.bucket}.s3.${config.region}.amazonaws.com"
            val url = "https://$host/$key"

            val now = Date()
            val dateFormat = SimpleDateFormat("yyyyMMdd'T'HHmmss'Z'", Locale.US)
            dateFormat.timeZone = TimeZone.getTimeZone("UTC")
            val amzDate = dateFormat.format(now)
            val dateStamp = amzDate.substring(0, 8)

            val payloadHash = sha256Hex(bytes)
            val headers = sortedMapOf(
                "host" to host,
                "x-amz-content-sha256" to payloadHash,
                "x-amz-date" to amzDate,
                "content-type" to mimeType
            )

            val signedHeaders = headers.keys.joinToString(";")
            val canonicalHeaders = headers.entries.joinToString("") { "${it.key}:${it.value}\n" }

            val canonicalRequest = listOf(
                "PUT", "/$key", "",
                canonicalHeaders, signedHeaders, payloadHash
            ).joinToString("\n")

            val credentialScope = "$dateStamp/${config.region}/s3/aws4_request"
            val stringToSign = listOf(
                "AWS4-HMAC-SHA256", amzDate, credentialScope,
                sha256Hex(canonicalRequest.toByteArray())
            ).joinToString("\n")

            val signingKey = getSignatureKey(config.secretKey, dateStamp, config.region, "s3")
            val signature = hmacSha256Hex(signingKey, stringToSign)

            val authorization = "AWS4-HMAC-SHA256 Credential=${config.accessKey}/$credentialScope, " +
                    "SignedHeaders=$signedHeaders, Signature=$signature"

            val request = Request.Builder()
                .url(url)
                .put(bytes.toRequestBody(mimeType.toMediaType()))
                .header("Host", host)
                .header("x-amz-content-sha256", payloadHash)
                .header("x-amz-date", amzDate)
                .header("Content-Type", mimeType)
                .header("Authorization", authorization)
                .build()

            val response = client.newCall(request).execute()
            val duration = System.currentTimeMillis() - start
            val speedKbps = if (duration > 0) {
                (sizeBytes.toDouble() / 1024.0) / (duration.toDouble() / 1000.0) * 8.0
            } else 0.0

            if (response.isSuccessful) {
                return UploadResult(uri, key, fileName, sizeBytes, duration, speedKbps, true)
            } else {
                return UploadResult(uri, null, fileName, sizeBytes, duration, 0.0, false,
                    "S3 ${response.code}: ${response.message}")
            }
        } catch (e: Exception) {
            val duration = System.currentTimeMillis() - start
            return UploadResult(uri, null, fileName, sizeBytes, duration, 0.0, false, e.message)
        }
    }

    private fun sha256Hex(data: ByteArray): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(data)
        return digest.joinToString("") { "%02x".format(it) }
    }

    private fun hmacSha256(key: ByteArray, data: String): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(data.toByteArray(Charsets.UTF_8))
    }

    private fun hmacSha256Hex(key: ByteArray, data: String): String {
        return hmacSha256(key, data).joinToString("") { "%02x".format(it) }
    }

    private fun getSignatureKey(key: String, dateStamp: String, region: String, service: String): ByteArray {
        var kDate = hmacSha256("AWS4$key".toByteArray(Charsets.UTF_8), dateStamp)
        var kRegion = hmacSha256(kDate, region)
        var kService = hmacSha256(kRegion, service)
        return hmacSha256(kService, "aws4_request")
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
