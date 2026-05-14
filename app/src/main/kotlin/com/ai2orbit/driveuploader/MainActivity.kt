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

        binding.btnSignOut.setOnClickListener {
            GoogleAuthHelper.signOut(this) {
                updateUI(false)
                Toast.makeText(this, "Signed out", Toast.LENGTH_SHORT).show()
            }
        }

        updateUI(GoogleAuthHelper.isSignedIn(this))
    }

    override fun onResume() {
        super.onResume()
        updateUI(GoogleAuthHelper.isSignedIn(this))
    }

    private fun onSignedIn(email: String) {
        binding.progressBar.visibility = View.GONE
        binding.tvStatus.text = "Signed in: $email"
        updateUI(true)
        Toast.makeText(this, "Welcome, $email", Toast.LENGTH_SHORT).show()
    }

    private fun updateUI(signedIn: Boolean) {
        binding.btnSignIn.visibility = if (signedIn) View.GONE else View.VISIBLE
        binding.btnSignOut.visibility = if (signedIn) View.VISIBLE else View.GONE
        binding.btnBenchmark.isEnabled = true
        binding.btnUpload.isEnabled = signedIn
        binding.progressBar.visibility = View.GONE

        if (signedIn) {
            val account = GoogleAuthHelper.getLastSignedInAccount(this)
            binding.tvStatus.text = "Signed in: ${account?.email ?: "Unknown"}"
            binding.tvUploadHint.text = "Ready to upload photos to Google Drive"
        } else {
            binding.tvStatus.text = "Sign in with Google to upload photos"
            binding.tvUploadHint.text = "Sign in first to enable photo upload"
        }
    }
}
