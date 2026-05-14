package com.ai2orbit.driveuploader

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.ai2orbit.driveuploader.databinding.ActivityMainBinding
import com.google.android.gms.auth.api.signin.GoogleSignIn
import com.google.android.gms.common.api.ApiException

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private val signInLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val task = GoogleSignIn.getSignedInAccountFromIntent(result.data)
        try {
            val account = task.getResult(ApiException::class.java)
            onSignedIn(account.email ?: "Unknown")
        } catch (e: ApiException) {
            binding.tvStatus.text = "Sign-in failed: ${e.statusCode}"
            binding.btnSignIn.isEnabled = true
            binding.progressBar.visibility = View.GONE
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnSignIn.setOnClickListener {
            binding.btnSignIn.isEnabled = false
            binding.progressBar.visibility = View.VISIBLE
            binding.tvStatus.text = "Signing in to Google..."
            signInLauncher.launch(GoogleAuthHelper.getSignInIntent(this))
        }

        binding.btnBenchmark.setOnClickListener {
            startActivity(Intent(this, BenchmarkActivity::class.java))
        }

        binding.btnUpload.setOnClickListener {
            startActivity(Intent(this, PhotoUploadActivity::class.java))
        }

        binding.btnCloudSetup.setOnClickListener {
            startActivity(Intent(this, CloudSetupActivity::class.java))
        }

        binding.btnSignOut.setOnClickListener {
            GoogleAuthHelper.signOut(this) {
                updateUI()
                Toast.makeText(this, "Signed out", Toast.LENGTH_SHORT).show()
            }
        }

        binding.rgCloud.setOnCheckedChangeListener { _, checkedId ->
            val provider = when (checkedId) {
                R.id.rbS3 -> CloudProvider.AMAZON_S3
                R.id.rbAzure -> CloudProvider.AZURE_BLOB
                else -> CloudProvider.GOOGLE_DRIVE
            }
            CloudConfigManager.setSelectedProvider(this, provider)
            updateUI()
        }

        updateUI()
    }

    override fun onResume() {
        super.onResume()
        updateUI()
    }

    private fun onSignedIn(email: String) {
        binding.progressBar.visibility = View.GONE
        updateUI()
        Toast.makeText(this, "Welcome, $email", Toast.LENGTH_SHORT).show()
    }

    private fun updateUI() {
        val provider = CloudConfigManager.getSelectedProvider(this)
        val configured = CloudConfigManager.isConfigured(this, provider)
        val googleSignedIn = GoogleAuthHelper.isSignedIn(this)

        when (provider) {
            CloudProvider.GOOGLE_DRIVE -> binding.rgCloud.check(R.id.rbGoogle)
            CloudProvider.AMAZON_S3 -> binding.rgCloud.check(R.id.rbS3)
            CloudProvider.AZURE_BLOB -> binding.rgCloud.check(R.id.rbAzure)
        }

        binding.btnSignIn.visibility = if (provider == CloudProvider.GOOGLE_DRIVE && !googleSignedIn)
            View.VISIBLE else View.GONE
        binding.btnSignOut.visibility = if (provider == CloudProvider.GOOGLE_DRIVE && googleSignedIn)
            View.VISIBLE else View.GONE
        binding.btnCloudSetup.visibility = if (provider != CloudProvider.GOOGLE_DRIVE)
            View.VISIBLE else View.GONE

        binding.btnBenchmark.isEnabled = true
        binding.btnUpload.isEnabled = configured
        binding.progressBar.visibility = View.GONE

        val label = CloudConfigManager.getProviderLabel(provider)
        if (configured) {
            when (provider) {
                CloudProvider.GOOGLE_DRIVE -> {
                    val email = GoogleAuthHelper.getLastSignedInAccount(this)?.email ?: "Unknown"
                    binding.tvStatus.text = "Google: $email"
                }
                CloudProvider.AMAZON_S3 -> {
                    val s3 = CloudConfigManager.loadS3Config(this)
                    binding.tvStatus.text = "S3: ${s3?.bucket} (${s3?.region}) - NO RATE LIMIT"
                }
                CloudProvider.AZURE_BLOB -> {
                    val az = CloudConfigManager.loadAzureConfig(this)
                    binding.tvStatus.text = "Azure: ${az?.accountName}/${az?.container} - NO RATE LIMIT"
                }
            }
            binding.tvUploadHint.text = "Ready to upload photos to $label"
        } else {
            binding.tvStatus.text = when (provider) {
                CloudProvider.GOOGLE_DRIVE -> "Sign in with Google"
                else -> "Configure $label credentials"
            }
            binding.tvUploadHint.text = when (provider) {
                CloudProvider.GOOGLE_DRIVE -> "Sign in to enable upload"
                else -> "Tap Cloud Setup to enter credentials"
            }
        }
    }
}
