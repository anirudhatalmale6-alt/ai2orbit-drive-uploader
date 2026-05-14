package com.ai2orbit.driveuploader

import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.ai2orbit.driveuploader.databinding.ActivityBenchmarkBinding
import kotlinx.coroutines.launch

class BenchmarkActivity : AppCompatActivity() {

    private lateinit var binding: ActivityBenchmarkBinding
    private var lastProfile: DeviceProfile? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityBenchmarkBinding.inflate(layoutInflater)
        setContentView(binding.root)

        supportActionBar?.title = "Device Performance"
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        binding.btnRunBenchmark.setOnClickListener { runBenchmark() }

        val cached = ProfileCache.load(this)
        if (cached != null) {
            lastProfile = cached
            displayProfile(cached)
            binding.tvHint.text = "Cached profile loaded. Tap to re-run."
        }
    }

    private fun runBenchmark() {
        binding.btnRunBenchmark.isEnabled = false
        binding.progressBar.visibility = View.VISIBLE
        binding.tvResults.text = "Running CPU / Memory / GPU benchmark...\nThis takes a few seconds."
        binding.tvHint.text = ""

        lifecycleScope.launch {
            val profile = PerformanceProfiler.profileDevice(this@BenchmarkActivity)
            lastProfile = profile
            ProfileCache.save(this@BenchmarkActivity, profile)
            displayProfile(profile)
            binding.btnRunBenchmark.isEnabled = true
            binding.progressBar.visibility = View.GONE
            binding.tvHint.text = "Profile saved. Upload will use these settings."
        }
    }

    private fun displayProfile(profile: DeviceProfile) {
        binding.tvResults.text = PerformanceProfiler.formatProfile(profile)
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}

object ProfileCache {
    private const val PREF = "ai2orbit_profile"

    fun save(ctx: android.content.Context, p: DeviceProfile) {
        ctx.getSharedPreferences(PREF, android.content.Context.MODE_PRIVATE).edit()
            .putFloat("cpu", p.cpuScoreNs.toFloat())
            .putFloat("mem", p.memoryMbps.toFloat())
            .putFloat("gpu", p.gpuScore.toFloat())
            .putFloat("screen", p.screenScore.toFloat())
            .putLong("totalRam", p.totalRamMb)
            .putLong("availRam", p.availableRamMb)
            .putInt("cores", p.cpuCores)
            .putFloat("power", p.powerFactor.toFloat())
            .putInt("chunk", p.optimalChunkKb)
            .putInt("streams", p.parallelStreams)
            .putFloat("kbps", p.estimatedKbps.toFloat())
            .putBoolean("valid", true)
            .apply()
    }

    fun load(ctx: android.content.Context): DeviceProfile? {
        val sp = ctx.getSharedPreferences(PREF, android.content.Context.MODE_PRIVATE)
        if (!sp.getBoolean("valid", false)) return null
        return DeviceProfile(
            cpuScoreNs = sp.getFloat("cpu", 0f).toDouble(),
            memoryMbps = sp.getFloat("mem", 0f).toDouble(),
            gpuScore = sp.getFloat("gpu", 0f).toDouble(),
            screenScore = sp.getFloat("screen", 0f).toDouble(),
            totalRamMb = sp.getLong("totalRam", 0),
            availableRamMb = sp.getLong("availRam", 0),
            cpuCores = sp.getInt("cores", 4),
            powerFactor = sp.getFloat("power", 1f).toDouble(),
            optimalChunkKb = sp.getInt("chunk", 2048),
            parallelStreams = sp.getInt("streams", 2),
            estimatedKbps = sp.getFloat("kbps", 0f).toDouble()
        )
    }
}
