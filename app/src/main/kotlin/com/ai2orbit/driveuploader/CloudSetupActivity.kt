package com.ai2orbit.driveuploader

import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.ai2orbit.driveuploader.databinding.ActivityCloudSetupBinding

class CloudSetupActivity : AppCompatActivity() {

    private lateinit var binding: ActivityCloudSetupBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCloudSetupBinding.inflate(layoutInflater)
        setContentView(binding.root)

        supportActionBar?.title = "Cloud Setup"
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        loadExisting()

        binding.btnSaveS3.setOnClickListener { saveS3() }
        binding.btnSaveAzure.setOnClickListener { saveAzure() }
        binding.btnClearS3.setOnClickListener { clearS3() }
        binding.btnClearAzure.setOnClickListener { clearAzure() }
    }

    private fun loadExisting() {
        val s3 = CloudConfigManager.loadS3Config(this)
        if (s3 != null) {
            binding.etS3Access.setText(s3.accessKey)
            binding.etS3Secret.setText(s3.secretKey)
            binding.etS3Bucket.setText(s3.bucket)
            binding.etS3Region.setText(s3.region)
            binding.tvS3Status.text = "Configured: ${s3.bucket} (${s3.region})"
            binding.tvS3Status.visibility = View.VISIBLE
        }

        val az = CloudConfigManager.loadAzureConfig(this)
        if (az != null) {
            binding.etAzAccount.setText(az.accountName)
            binding.etAzKey.setText(az.accountKey)
            binding.etAzContainer.setText(az.container)
            binding.tvAzStatus.text = "Configured: ${az.accountName}/${az.container}"
            binding.tvAzStatus.visibility = View.VISIBLE
        }
    }

    private fun saveS3() {
        val access = binding.etS3Access.text.toString().trim()
        val secret = binding.etS3Secret.text.toString().trim()
        val bucket = binding.etS3Bucket.text.toString().trim().ifEmpty { "ai2orbit-photos" }
        val region = binding.etS3Region.text.toString().trim().ifEmpty { "us-east-1" }

        if (access.isEmpty() || secret.isEmpty()) {
            Toast.makeText(this, "Access Key and Secret Key required", Toast.LENGTH_SHORT).show()
            return
        }

        CloudConfigManager.saveS3Config(this, S3Config(access, secret, bucket, region))
        binding.tvS3Status.text = "Saved: $bucket ($region)"
        binding.tvS3Status.visibility = View.VISIBLE
        Toast.makeText(this, "S3 config saved", Toast.LENGTH_SHORT).show()
    }

    private fun saveAzure() {
        val account = binding.etAzAccount.text.toString().trim()
        val key = binding.etAzKey.text.toString().trim()
        val container = binding.etAzContainer.text.toString().trim().ifEmpty { "ai2orbit-photos" }

        if (account.isEmpty() || key.isEmpty()) {
            Toast.makeText(this, "Account Name and Key required", Toast.LENGTH_SHORT).show()
            return
        }

        CloudConfigManager.saveAzureConfig(this, AzureConfig(account, key, container))
        binding.tvAzStatus.text = "Saved: $account/$container"
        binding.tvAzStatus.visibility = View.VISIBLE
        Toast.makeText(this, "Azure config saved", Toast.LENGTH_SHORT).show()
    }

    private fun clearS3() {
        getSharedPreferences("ai2orbit_cloud", MODE_PRIVATE).edit()
            .remove("s3_access").remove("s3_secret").remove("s3_bucket").remove("s3_region")
            .apply()
        binding.etS3Access.text?.clear()
        binding.etS3Secret.text?.clear()
        binding.etS3Bucket.text?.clear()
        binding.etS3Region.text?.clear()
        binding.tvS3Status.visibility = View.GONE
        Toast.makeText(this, "S3 config cleared", Toast.LENGTH_SHORT).show()
    }

    private fun clearAzure() {
        getSharedPreferences("ai2orbit_cloud", MODE_PRIVATE).edit()
            .remove("az_account").remove("az_key").remove("az_container")
            .apply()
        binding.etAzAccount.text?.clear()
        binding.etAzKey.text?.clear()
        binding.etAzContainer.text?.clear()
        binding.tvAzStatus.visibility = View.GONE
        Toast.makeText(this, "Azure config cleared", Toast.LENGTH_SHORT).show()
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
