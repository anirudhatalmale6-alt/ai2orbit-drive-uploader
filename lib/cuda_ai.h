/**
 * cuda_ai.h - CUDA GPU compute benchmark
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 */

#ifndef CUDA_AI_H
#define CUDA_AI_H

#include <cstddef>
#include <string>

/**
 * CudaAi - GPU compute benchmark with CPU-simulated fallback.
 *
 * When CUDA hardware is available, runs real GPU benchmarks.
 * Otherwise, simulates GPU-like workloads on CPU (matrix multiply,
 * memory copy, PCIe transfer) and estimates GPU throughput using
 * empirical scaling factors.
 */
class CudaAi {
public:
    /** Results of a CUDA benchmark run. */
    struct Results {
        double compute_gflops;              ///< GPU compute throughput in GFLOPS
        double memory_bandwidth_gbps;       ///< GPU memory bandwidth in GB/s
        double cpu_to_gpu_transfer_gbps;    ///< Host-to-device transfer speed in GB/s
        double gpu_to_cpu_transfer_gbps;    ///< Device-to-host transfer speed in GB/s
        double total_time_seconds;          ///< Wall-clock time for the entire benchmark
        bool cuda_available;                ///< True if real CUDA hardware was detected
        bool benchmark_successful;          ///< True if the benchmark completed
        std::string device_name;            ///< GPU device name or "Simulated" for fallback
        int compute_capability_major;       ///< CUDA compute capability major version
        int compute_capability_minor;       ///< CUDA compute capability minor version
        std::size_t device_memory_bytes;    ///< Total GPU device memory in bytes
    };

    CudaAi() noexcept;

    /**
     * Runs the CUDA benchmark (real or simulated fallback).
     * @param problem_size Number of elements for compute and memory tests (default 1M).
     * @return Results struct with throughput metrics and device info.
     */
    Results run(std::size_t problem_size = 1024 * 1024);

    /**
     * Formats benchmark results as a human-readable string.
     * @param results The Results struct to format.
     * @return Formatted multi-line string.
     */
    static std::string format_results(const Results& results);

private:
    /**
     * Checks whether a CUDA-capable device is available.
     * @return True if CUDA runtime is accessible and a device is found.
     */
    bool detect_cuda() noexcept;

    /**
     * Runs the CPU-simulated fallback benchmark with GPU scaling factors.
     * @param problem_size Number of elements for the workload.
     * @return Results with estimated GPU-equivalent throughput.
     */
    Results run_simulated(std::size_t problem_size);
};

#endif // CUDA_AI_H
