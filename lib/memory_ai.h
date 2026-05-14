/**
 * memory_ai.h - RAM benchmark with read-write-verify cycles
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 */

#ifndef MEMORY_AI_H
#define MEMORY_AI_H

#include <cstdint>
#include <vector>
#include <iostream>

/**
 * MemoryAi - Measures RAM throughput and latency using
 * read-write-verify cycles with full statistical analysis.
 *
 * Each iteration performs: read all bytes, write a deterministic pattern,
 * then verify correctness. Latency statistics (min/max/avg/variance/stddev)
 * are collected per cycle.
 */
class MemoryAi {
public:
    /**
     * Timing statistics collected across all benchmark iterations.
     */
    struct Timing {
        double total_time_seconds;   ///< Wall-clock time for the entire run
        double min_latency_ns;       ///< Minimum single-cycle latency observed
        double max_latency_ns;       ///< Maximum single-cycle latency observed
        double avg_latency_ns;       ///< Mean latency across all cycles
        double variance_ns;          ///< Sample variance of cycle latencies
        double std_deviation_ns;     ///< Sample standard deviation of cycle latencies
        std::size_t sample_count;    ///< Number of latency samples collected
    };

    /**
     * Final results of a memory benchmark run.
     */
    struct Results {
        std::size_t buffer_size_bytes;   ///< Size of the test buffer
        std::size_t iterations;          ///< Number of read-write-verify cycles
        double throughput_mbps;          ///< Measured throughput in MB/s
        bool verification_passed;        ///< True if all verify cycles matched
        std::size_t verification_errors; ///< Total byte mismatches detected
        Timing timing;                   ///< Detailed latency statistics
    };

    MemoryAi() noexcept;

    /**
     * Runs a standard benchmark with a freshly allocated buffer.
     * @param buffer_size_bytes Size of the memory buffer to allocate and test.
     * @param iterations        Number of read-write-verify cycles to perform.
     * @return Results struct with throughput, latency stats, and verification status.
     */
    Results run(std::size_t buffer_size_bytes, std::size_t iterations);

    /**
     * Runs multiple benchmark passes, aggregating results across runs.
     * Stops when either max_runs or max_duration_seconds is reached.
     * @param buffer_size_bytes    Size of the memory buffer per run.
     * @param iterations_per_run   Cycles per individual run.
     * @param max_runs             Maximum number of runs (0 = unlimited).
     * @param max_duration_seconds Maximum wall-clock time (0.0 = unlimited).
     * @return Aggregated Results across all completed runs.
     */
    Results run_continuous(std::size_t buffer_size_bytes,
                           std::size_t iterations_per_run,
                           std::size_t max_runs,
                           double max_duration_seconds);

    /**
     * Prints formatted benchmark results to stdout as a detailed table.
     * @param results The Results struct to display.
     */
    static void print_results(const Results& results);

private:
    /**
     * Core benchmark logic operating on a pre-allocated raw buffer.
     * @param buffer            Pointer to the test buffer.
     * @param buffer_size_bytes Size of the buffer in bytes.
     * @param iterations        Number of read-write-verify cycles.
     * @return Results for this buffer run.
     */
    Results run_with_buffer(std::uint8_t* buffer,
                            std::size_t buffer_size_bytes,
                            std::size_t iterations);

    /**
     * Computes sample variance and standard deviation from latency samples.
     * @param latencies     Vector of per-cycle latency values.
     * @param mean          Pre-computed mean of the latencies.
     * @param variance      [out] Computed sample variance.
     * @param std_deviation [out] Computed sample standard deviation.
     */
    void calculate_statistics(const std::vector<double>& latencies,
                              double mean,
                              double& variance,
                              double& std_deviation) noexcept;

    /**
     * Performs a single fused read-write-verify cycle on the buffer.
     * @param buffer           Pointer to the test buffer.
     * @param size             Buffer size in bytes.
     * @param cycle_latency_ns [out] Elapsed nanoseconds for this cycle.
     * @return Number of verification errors (byte mismatches) detected.
     */
    std::size_t verify_cycle(std::uint8_t* buffer,
                             std::size_t size,
                             std::int64_t& cycle_latency_ns) noexcept;

    /**
     * Writes a deterministic byte pattern (i & 0xFF) to the buffer.
     * @param buffer Pointer to the buffer.
     * @param size   Number of bytes to write.
     */
    void write_pattern(std::uint8_t* buffer, std::size_t size) noexcept;

    /**
     * Verifies the buffer contents against the deterministic pattern.
     * @param buffer Pointer to the buffer.
     * @param size   Number of bytes to verify.
     * @return Number of byte mismatches found.
     */
    std::size_t verify_pattern(const std::uint8_t* buffer,
                               std::size_t size) const noexcept;
};

#endif // MEMORY_AI_H
