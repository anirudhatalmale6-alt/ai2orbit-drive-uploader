package com.ai2orbit.driveuploader

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.GridLayoutManager
import com.ai2orbit.driveuploader.databinding.ActivityPhotoUploadBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class PhotoUploadActivity : AppCompatActivity() {

    private lateinit var binding: ActivityPhotoUploadBinding
    private val photoUris = mutableListOf<Uri>()
    private val selectedUris = mutableSetOf<Uri>()
    private var adapter: PhotoAdapter? = null
    private var profile: DeviceProfile? = null

    companion object {
        private const val PERM_REQUEST = 100
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityPhotoUploadBinding.inflate(layoutInflater)
        setContentView(binding.root)

        supportActionBar?.title = "Upload Photos"
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        profile = ProfileCache.load(this)
        if (profile == null) {
            binding.tvProfileStatus.text = "No benchmark profile. Run benchmark first for optimal speed."
            binding.tvProfileStatus.visibility = View.VISIBLE
        } else {
            binding.tvProfileStatus.text = "Profile: ${profile!!.parallelStreams} streams, ${profile!!.optimalChunkKb}KB chunks"
            binding.tvProfileStatus.visibility = View.VISIBLE
        }

        binding.rvPhotos.layoutManager = GridLayoutManager(this, 3)

        binding.btnSelectAll.setOnClickListener {
            if (selectedUris.size == photoUris.size) {
                selectedUris.clear()
            } else {
                selectedUris.addAll(photoUris)
            }
            adapter?.notifyDataSetChanged()
            updateUploadButton()
        }

        binding.btnUpload.setOnClickListener { startUpload() }

        checkPermissionAndLoad()
    }

    private fun checkPermissionAndLoad() {
        val perm = if (Build.VERSION.SDK_INT >= 33) {
            Manifest.permission.READ_MEDIA_IMAGES
        } else {
            Manifest.permission.READ_EXTERNAL_STORAGE
        }

        if (ContextCompat.checkSelfPermission(this, perm) == PackageManager.PERMISSION_GRANTED) {
            loadPhotos()
        } else {
            ActivityCompat.requestPermissions(this, arrayOf(perm), PERM_REQUEST)
        }
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, perms, results)
        if (code == PERM_REQUEST && results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED) {
            loadPhotos()
        } else {
            Toast.makeText(this, "Photo access required", Toast.LENGTH_LONG).show()
        }
    }

    private fun loadPhotos() {
        lifecycleScope.launch {
            val uris = withContext(Dispatchers.IO) { queryPhotos() }
            photoUris.clear()
            photoUris.addAll(uris)

            adapter = PhotoAdapter(photoUris, selectedUris) { updateUploadButton() }
            binding.rvPhotos.adapter = adapter

            binding.tvCount.text = "${photoUris.size} photos found"
        }
    }

    private fun queryPhotos(): List<Uri> {
        val result = mutableListOf<Uri>()
        val projection = arrayOf(MediaStore.Images.Media._ID)
        val sortOrder = "${MediaStore.Images.Media.DATE_ADDED} DESC"
        val collection = if (Build.VERSION.SDK_INT >= 29) {
            MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL)
        } else {
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        }

        contentResolver.query(collection, projection, null, null, sortOrder)?.use { cursor ->
            val idCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID)
            while (cursor.moveToNext()) {
                val id = cursor.getLong(idCol)
                val uri = android.content.ContentUris.withAppendedId(collection, id)
                result.add(uri)
            }
        }
        return result
    }

    private fun updateUploadButton() {
        binding.btnUpload.isEnabled = selectedUris.isNotEmpty()
        binding.btnUpload.text = if (selectedUris.isEmpty()) {
            "Select photos to upload"
        } else {
            "Upload ${selectedUris.size} photos to Drive"
        }
    }

    private fun startUpload() {
        val driveService = GoogleAuthHelper.getDriveService(this)
        if (driveService == null) {
            Toast.makeText(this, "Not signed in to Google", Toast.LENGTH_LONG).show()
            return
        }

        val effectiveProfile = profile ?: DeviceProfile(
            cpuScoreNs = 10000.0, memoryMbps = 500.0, gpuScore = 1.0,
            screenScore = 60.0, totalRamMb = 2048, availableRamMb = 1024,
            cpuCores = 4, powerFactor = 1.0, optimalChunkKb = 2048,
            parallelStreams = 2, estimatedKbps = 1000.0
        )

        val uploader = DriveUploader(driveService, contentResolver, effectiveProfile)
        val toUpload = selectedUris.toList()

        binding.btnUpload.isEnabled = false
        binding.btnSelectAll.isEnabled = false
        binding.progressBar.visibility = View.VISIBLE
        binding.tvProgress.visibility = View.VISIBLE
        binding.tvProgress.text = "Uploading 0 / ${toUpload.size}..."

        lifecycleScope.launch {
            val startTime = System.currentTimeMillis()

            val batchResult = uploader.uploadPhotos(toUpload) { current, total, result ->
                binding.tvProgress.text = "Uploading $current / $total..." +
                    if (result != null && result.success) {
                        "\n${result.fileName}: ${String.format("%.1f", result.speedKbps)} kbps"
                    } else if (result != null) {
                        "\n${result.fileName}: FAILED"
                    } else ""
            }

            val totalTime = System.currentTimeMillis() - startTime

            binding.progressBar.visibility = View.GONE
            binding.btnUpload.isEnabled = true
            binding.btnSelectAll.isEnabled = true

            val totalMb = batchResult.totalBytes.toDouble() / (1024 * 1024)
            binding.tvProgress.text = buildString {
                appendLine("UPLOAD COMPLETE")
                appendLine("─".repeat(30))
                appendLine("Files: ${batchResult.successCount}/${batchResult.totalFiles} uploaded")
                appendLine("Total: ${String.format("%.2f", totalMb)} MB")
                appendLine("Time: ${String.format("%.1f", totalTime / 1000.0)} sec")
                appendLine("Speed: ${String.format("%.2f", batchResult.avgSpeedKbps)} kbps")
                appendLine("Streams: ${effectiveProfile.parallelStreams}")
                appendLine("Chunk: ${effectiveProfile.optimalChunkKb} KB")
                if (batchResult.failCount > 0) {
                    appendLine("Failed: ${batchResult.failCount}")
                }
                appendLine("─".repeat(30))
                append("Photos saved to Google Drive > AI2ORBIT_Photos")
            }

            selectedUris.clear()
            adapter?.notifyDataSetChanged()
            updateUploadButton()
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
