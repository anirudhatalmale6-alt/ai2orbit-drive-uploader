package com.ai2orbit.driveuploader

import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns
import com.google.api.client.http.InputStreamContent
import com.google.api.services.drive.Drive
import com.google.api.services.drive.model.File as DriveFile
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.sync.Semaphore
import java.io.InputStream

data class UploadResult(
    val uri: Uri,
    val driveFileId: String?,
    val fileName: String,
    val sizeBytes: Long,
    val durationMs: Long,
    val speedKbps: Double,
    val success: Boolean,
    val error: String? = null
)

data class UploadBatchResult(
    val results: List<UploadResult>,
    val totalFiles: Int,
    val successCount: Int,
    val failCount: Int,
    val totalBytes: Long,
    val totalDurationMs: Long,
    val avgSpeedKbps: Double
)

class DriveUploader(
    private val driveService: Drive,
    private val contentResolver: ContentResolver,
    private val profile: DeviceProfile
) {
    private val folderId: String by lazy { getOrCreateFolder() }

    private fun getOrCreateFolder(): String {
        val query = "name='AI2ORBIT_Photos' and mimeType='application/vnd.google-apps.folder' and trashed=false"
        val existing = driveService.files().list()
            .setQ(query)
            .setSpaces("drive")
            .setFields("files(id)")
            .execute()

        if (existing.files.isNotEmpty()) {
            return existing.files[0].id
        }

        val folderMeta = DriveFile().apply {
            name = "AI2ORBIT_Photos"
            mimeType = "application/vnd.google-apps.folder"
        }
        val folder = driveService.files().create(folderMeta)
            .setFields("id")
            .execute()
        return folder.id
    }

    suspend fun uploadPhotos(
        uris: List<Uri>,
        onProgress: (current: Int, total: Int, result: UploadResult?) -> Unit
    ): UploadBatchResult = withContext(Dispatchers.IO) {
        val streamCount = profile.virtualStreams.coerceIn(7, 256)
        val semaphore = Semaphore(streamCount)
        val results = Channel<UploadResult>(Channel.UNLIMITED)

        val jobs = uris.mapIndexed { index, uri ->
            async {
                semaphore.acquire()
                try {
                    val result = uploadSingle(uri)
                    results.send(result)
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
        results.close()

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
            val inputStream: InputStream = contentResolver.openInputStream(uri)
                ?: throw Exception("Cannot open file")

            val mimeType = contentResolver.getType(uri) ?: "image/jpeg"
            val content = InputStreamContent(mimeType, inputStream)
            content.length = sizeBytes

            val fileMeta = DriveFile().apply {
                name = fileName
                parents = listOf(folderId)
            }

            val chunkSize = profile.optimalChunkKb * 1024
            val uploaded = driveService.files().create(fileMeta, content)
                .setFields("id,name,size")
                .setChunkSize(chunkSize)
                .execute()

            inputStream.close()
            val duration = System.currentTimeMillis() - start
            val speedKbps = if (duration > 0) {
                (sizeBytes.toDouble() / 1024.0) / (duration.toDouble() / 1000.0) * 8.0
            } else 0.0

            return UploadResult(
                uri = uri,
                driveFileId = uploaded.id,
                fileName = fileName,
                sizeBytes = sizeBytes,
                durationMs = duration,
                speedKbps = speedKbps,
                success = true
            )
        } catch (e: Exception) {
            val duration = System.currentTimeMillis() - start
            return UploadResult(
                uri = uri,
                driveFileId = null,
                fileName = fileName,
                sizeBytes = sizeBytes,
                durationMs = duration,
                speedKbps = 0.0,
                success = false,
                error = e.message
            )
        }
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
