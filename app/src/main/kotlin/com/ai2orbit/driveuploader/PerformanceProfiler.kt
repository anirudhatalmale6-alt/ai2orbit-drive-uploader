package com.ai2orbit.driveuploader

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.withContext
import kotlin.math.*

data class DeviceProfile(
    val cpuScoreNs: Double,
    val memoryMbps: Double,
    val gpuScore: Double,
    val screenScore: Double,
    val totalRamMb: Long,
    val availableRamMb: Long,
    val cpuCores: Int,
    val powerFactor: Double,
    val optimalChunkKb: Int,
    val parallelStreams: Int,
    val estimatedKbps: Double
)

object PerformanceProfiler {

    private const val LOOP_COUNT = 50000
    private const val SLOWEST_ACCEPT = 67
    private const val PICK_SLOWEST = 18

    suspend fun profileDevice(context: Context): DeviceProfile = withContext(Dispatchers.Default) {
        val cpuDeferred = async { benchmarkCpu() }
        val memDeferred = async { benchmarkMemory() }
        val gpuDeferred = async { benchmarkGpu() }

        val cpuScore = cpuDeferred.await()
        val memScore = memDeferred.await()
        val gpuScore = gpuDeferred.await()

        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memInfo = ActivityManager.MemoryInfo()
        am.getMemoryInfo(memInfo)
        val totalRamMb = memInfo.totalMem / (1024 * 1024)
        val availRamMb = memInfo.availMem / (1024 * 1024)

        val cores = Runtime.getRuntime().availableProcessors()
        val screenScore = 60.0

        val cpuNorm = max(cpuScore / 1000.0, 0.1)
        val memNorm = max(memScore / 100.0, 0.1)
        val gpuNorm = max(gpuScore / 100.0, 0.1)
        val screenNorm = screenScore / 10.0

        val powerFactor = cpuNorm.pow(2.0) *
                memNorm.pow(3.0) *
                gpuNorm.pow(1.0) *
                screenNorm.pow(1.0)

        val baseKbps = (10.0 * 8.0 * 1e9) / cpuScore / 1000.0
        val boostedKbps = baseKbps * powerFactor

        val optimalChunk = when {
            availRamMb > 2048 -> 8192
            availRamMb > 1024 -> 4096
            availRamMb > 512 -> 2048
            else -> 1024
        }

        val streams = when {
            cores >= 8 && availRamMb > 2048 -> 6
            cores >= 6 && availRamMb > 1024 -> 4
            cores >= 4 -> 3
            else -> 2
        }

        DeviceProfile(
            cpuScoreNs = cpuScore,
            memoryMbps = memScore,
            gpuScore = gpuScore,
            screenScore = screenScore,
            totalRamMb = totalRamMb,
            availableRamMb = availRamMb,
            cpuCores = cores,
            powerFactor = powerFactor,
            optimalChunkKb = optimalChunk,
            parallelStreams = streams,
            estimatedKbps = boostedKbps
        )
    }

    private fun benchmarkCpu(): Double {
        val times = DoubleArray(LOOP_COUNT)
        for (i in 0 until LOOP_COUNT) {
            val start = System.nanoTime()
            var x = i.toDouble() * 0.0001 + 1.0
            for (k in 0 until 10) {
                x = sin(x) * cos(x * 0.5) + ln(x + 1.0)
                x = exp(-x * 0.01) * sqrt(abs(x) + 1.0)
            }
            @Suppress("UNUSED_VARIABLE") val sink = x
            times[i] = (System.nanoTime() - start).toDouble()
        }
        times.sort()

        val slowest = times.sliceArray((LOOP_COUNT - SLOWEST_ACCEPT) until LOOP_COUNT)
        val picked = slowest.sliceArray((SLOWEST_ACCEPT - PICK_SLOWEST) until SLOWEST_ACCEPT)

        return picked.average()
    }

    private fun benchmarkMemory(): Double {
        val size = 4 * 1024 * 1024
        val buffer = ByteArray(size)
        val start = System.nanoTime()
        var checksum = 0L
        for (pass in 0 until 20) {
            for (i in buffer.indices step 64) {
                buffer[i] = (i and 0xFF).toByte()
                checksum += buffer[i]
            }
        }
        @Suppress("UNUSED_VARIABLE") val sink = checksum
        val elapsed = (System.nanoTime() - start) / 1e9
        val totalBytes = size.toLong() * 20
        return (totalBytes / elapsed) / (1024 * 1024)
    }

    private fun benchmarkGpu(): Double {
        val size = 2 * 1024 * 1024
        val start = System.nanoTime()
        var acc = 0.0
        for (i in 0 until size) {
            val x = i.toDouble() / size
            acc += sin(x * PI) * cos(x * 2 * PI) + sqrt(x + 0.001)
        }
        @Suppress("UNUSED_VARIABLE") val sink = acc
        val elapsed = (System.nanoTime() - start) / 1e9
        return (size.toDouble() * 3) / elapsed / 1e9
    }

    fun formatProfile(profile: DeviceProfile): String {
        return buildString {
            appendLine("DEVICE PERFORMANCE PROFILE")
            appendLine("─".repeat(40))
            appendLine("CPU Score:       ${String.format("%.2f", profile.cpuScoreNs)} ns")
            appendLine("Memory:          ${String.format("%.2f", profile.memoryMbps)} MB/s")
            appendLine("GPU Score:       ${String.format("%.4f", profile.gpuScore)} GFLOPS")
            appendLine("Screen Score:    ${String.format("%.1f", profile.screenScore)} FPS")
            appendLine("CPU Cores:       ${profile.cpuCores}")
            appendLine("Total RAM:       ${profile.totalRamMb} MB")
            appendLine("Available RAM:   ${profile.availableRamMb} MB")
            appendLine("─".repeat(40))
            appendLine("Power Factor:    ${String.format("%.4e", profile.powerFactor)}")
            appendLine("Optimal Chunk:   ${profile.optimalChunkKb} KB")
            appendLine("Parallel Streams: ${profile.parallelStreams}")
            appendLine("Est. Throughput: ${String.format("%.2e", profile.estimatedKbps)} kbps")
            appendLine("─".repeat(40))
        }
    }
}
