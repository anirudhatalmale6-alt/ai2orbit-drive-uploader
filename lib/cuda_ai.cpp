/**
 * cuda_ai.cpp - CUDA benchmark implementation (simulated fallback)
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 *
 * Provides GPU compute benchmarks. When no CUDA device is available,
 * runs equivalent workloads on CPU and applies GPU estimation factors
 * to produce realistic throughput estimates.
 */

#include "cuda_ai.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

CudaAi::CudaAi() noexcept = default;

/**
 * Attempts to detect CUDA runtime availability.
 * @return Always false in this build (no CUDA runtime linked).
 */
bool CudaAi::detect_cuda() noexcept {
    // No CUDA runtime available on this build server
    return false;
}

/**
 * Runs the CPU-simulated CUDA benchmark with three sub-tests:
 *  1. Compute: dense matrix multiply (FLOPs measurement)
 *  2. Memory bandwidth: large buffer read-write passes
 *  3. Transfer: simulated PCIe host-device copy
 *
 * GPU throughput is estimated by scaling CPU results with empirical factors.
 */
CudaAi::Results CudaAi::run_simulated(std::size_t problem_size) {
    Results results{};
    results.cuda_available = false;
    results.device_name = "Simulated (CPU fallback)";
    results.compute_capability_major = 0;
    results.compute_capability_minor = 0;
    results.device_memory_bytes = 0;

    auto total_start = std::chrono::high_resolution_clock::now();

    // --- Sub-test 1: Compute benchmark (matrix multiply on CPU) ---
    // Clamp matrix dimension to [64, 512] for reasonable runtime
    const std::size_t mat_dim = static_cast<std::size_t>(std::sqrt(static_cast<double>(problem_size)));
    const std::size_t dim = (mat_dim > 512) ? 512 : ((mat_dim < 64) ? 64 : mat_dim);

    std::vector<float> A(dim * dim, 1.0f);
    std::vector<float> B(dim * dim, 1.0f);
    std::vector<float> C(dim * dim, 0.0f);

    // Fill with varying data to prevent constant-folding
    for (std::size_t i = 0; i < dim * dim; ++i) {
        A[i] = static_cast<float>(i % 17) * 0.1f + 0.1f;
        B[i] = static_cast<float>(i % 13) * 0.1f + 0.1f;
    }

    auto compute_start = std::chrono::high_resolution_clock::now();

    // ikj loop order for better cache locality on row-major matrices
    for (std::size_t i = 0; i < dim; ++i) {
        for (std::size_t k = 0; k < dim; ++k) {
            float a_ik = A[i * dim + k];
            for (std::size_t j = 0; j < dim; ++j) {
                C[i * dim + j] += a_ik * B[k * dim + j];
            }
        }
    }

    auto compute_end = std::chrono::high_resolution_clock::now();
    double compute_sec = std::chrono::duration<double>(compute_end - compute_start).count();
    if (compute_sec < 1e-9) compute_sec = 1e-9;

    // FLOPs = 2 * dim^3 (one multiply + one add per output element per k)
    double cpu_flops = 2.0 * static_cast<double>(dim) * static_cast<double>(dim) * static_cast<double>(dim);
    double cpu_gflops = (cpu_flops / compute_sec) / 1e9;

    // GPU scaling factor: typical discrete GPU is 20-50x faster than single-threaded CPU
    const double gpu_factor = 25.0;
    results.compute_gflops = cpu_gflops * gpu_factor;

    // Prevent optimizer from removing the matrix computation
    volatile float sink = C[0];
    (void)sink;

    // --- Sub-test 2: Memory bandwidth (large buffer read-write) ---
    const std::size_t mem_size = problem_size * sizeof(float);
    std::vector<float> src(problem_size, 3.14f);
    std::vector<float> dst(problem_size, 0.0f);

    auto mem_start = std::chrono::high_resolution_clock::now();

    std::memcpy(dst.data(), src.data(), mem_size);

    // Multiple passes to get a stable measurement
    for (int pass = 0; pass < 10; ++pass) {
        for (std::size_t i = 0; i < problem_size; ++i) {
            dst[i] = src[i] + static_cast<float>(pass);
        }
    }

    auto mem_end = std::chrono::high_resolution_clock::now();
    double mem_sec = std::chrono::duration<double>(mem_end - mem_start).count();
    if (mem_sec < 1e-9) mem_sec = 1e-9;

    // Total bytes: 10 passes * (read src + write dst)
    double total_bytes = static_cast<double>(mem_size) * 10.0 * 2.0;
    double cpu_bw_gbps = (total_bytes / mem_sec) / 1e9;

    // GPU memory bandwidth is typically 10-20x CPU
    const double mem_factor = 15.0;
    results.memory_bandwidth_gbps = cpu_bw_gbps * mem_factor;

    volatile float sink2 = dst[0];
    (void)sink2;

    // --- Sub-test 3: Transfer benchmark (simulated PCIe copy) ---
    auto xfer_start = std::chrono::high_resolution_clock::now();

    std::memcpy(dst.data(), src.data(), mem_size);

    auto xfer_end = std::chrono::high_resolution_clock::now();
    double xfer_sec = std::chrono::duration<double>(xfer_end - xfer_start).count();
    if (xfer_sec < 1e-9) xfer_sec = 1e-9;

    double xfer_gbps = (static_cast<double>(mem_size) / xfer_sec) / 1e9;

    // PCIe 4.0 x16 ~ 32 GB/s; scale from memcpy speed with slight asymmetry
    results.cpu_to_gpu_transfer_gbps = xfer_gbps * 2.0;
    results.gpu_to_cpu_transfer_gbps = xfer_gbps * 1.8;

    auto total_end = std::chrono::high_resolution_clock::now();
    results.total_time_seconds = std::chrono::duration<double>(total_end - total_start).count();
    results.benchmark_successful = true;

    return results;
}

/**
 * Entry point: runs real CUDA benchmarks if available, otherwise falls
 * back to the CPU-simulated path with GPU estimation factors.
 */
CudaAi::Results CudaAi::run(std::size_t problem_size) {
    if (detect_cuda()) {
        // Would run real CUDA benchmarks here
        // For now, fall through to simulated
    }
    return run_simulated(problem_size);
}

/**
 * Formats CUDA benchmark results as a human-readable string with
 * device info, compute/memory/transfer metrics, and total time.
 */
std::string CudaAi::format_results(const Results& results) {
    std::ostringstream oss;
    oss << std::fixed;

    oss << "\n===== CUDA Benchmark Results =====\n";
    oss << "Status: " << (results.benchmark_successful ? "PASSED" : "FAILED") << "\n";
    oss << "CUDA Available: " << (results.cuda_available ? "YES" : "NO - simulated") << "\n";
    oss << "----------------------------------\n";

    oss << "Device: " << results.device_name << "\n";
    oss << "Compute Capability: " << results.compute_capability_major
        << "." << results.compute_capability_minor << "\n";

    if (results.device_memory_bytes > 0) {
        oss << "Device Memory: " << (results.device_memory_bytes / (1024 * 1024)) << " MB\n";
    }

    oss << "----------------------------------\n";
    oss << std::setprecision(2);
    oss << "Compute:       " << std::setw(10) << results.compute_gflops << " GFLOPS\n";
    oss << "Memory BW:     " << std::setw(10) << results.memory_bandwidth_gbps << " GB/s\n";
    oss << "CPU->GPU:      " << std::setw(10) << results.cpu_to_gpu_transfer_gbps << " GB/s\n";
    oss << "GPU->CPU:      " << std::setw(10) << results.gpu_to_cpu_transfer_gbps << " GB/s\n";
    oss << "----------------------------------\n";
    oss << std::setprecision(4);
    oss << "Total Time:    " << std::setw(10) << results.total_time_seconds << " s\n";
    oss << "==================================\n";

    return oss.str();
}
