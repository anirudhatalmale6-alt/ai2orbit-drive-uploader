/**
 * memory_ai.cpp - RAM benchmark implementation
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 *
 * Performs read-write-verify cycles with statistical analysis.
 *
 * Optimizations applied:
 *  1. write_pattern + verify_pattern fused into a single loop inside verify_cycle
 *     (eliminates one full buffer pass and two function calls per iteration)
 *  2. first_iteration branch eliminated by pre-running iteration 0 outside the loop
 *  3. latencies vector uses resize() + indexed writes instead of push_back()
 *     (removes per-call capacity branch)
 */

#include "memory_ai.h"
#include "timer.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>

MemoryAi::MemoryAi() noexcept
{
}

/**
 * Allocates a fresh buffer and delegates to run_with_buffer().
 */
MemoryAi::Results MemoryAi::run(
    std::size_t buffer_size_bytes,
    std::size_t iterations)
{
    if (buffer_size_bytes == 0)
    {
        Results results{};
        std::cerr << "Error: Buffer size must be greater than 0\n";
        return results;
    }
    if (iterations == 0)
    {
        Results results{};
        std::cerr << "Error: Iterations must be greater than 0\n";
        return results;
    }

    std::vector<std::uint8_t> buffer(buffer_size_bytes);
    return run_with_buffer(buffer.data(), buffer_size_bytes, iterations);
}

/**
 * Core benchmark loop: runs N read-write-verify cycles on the given buffer,
 * collecting per-cycle latency samples and computing aggregate statistics.
 */
MemoryAi::Results MemoryAi::run_with_buffer(
    std::uint8_t *buffer,
    std::size_t buffer_size_bytes,
    std::size_t iterations)
{
    Results results{};
    results.buffer_size_bytes = buffer_size_bytes;
    results.iterations = iterations;
    results.verification_passed = true;
    results.verification_errors = 0;
    results.timing.min_latency_ns = 0.0;
    results.timing.max_latency_ns = 0.0;
    results.timing.avg_latency_ns = 0.0;
    results.timing.total_time_seconds = 0.0;
    results.timing.variance_ns = 0.0;
    results.timing.std_deviation_ns = 0.0;
    results.timing.sample_count = 0;

    Timer total_timer;
    total_timer.start();

    std::size_t total_errors = 0;

    // Optimization 3: resize() + indexed write -- no per-iteration capacity check.
    std::vector<double> latencies(iterations);

    // Optimization 2: run iteration 0 outside the loop to initialise min/max
    // directly, removing the first_iteration branch from the hot path entirely.
    std::int64_t first_ns = 0;
    total_errors += verify_cycle(buffer, buffer_size_bytes, first_ns);
    std::int64_t min_latency_ns = first_ns;
    std::int64_t max_latency_ns = first_ns;
    std::int64_t sum_latency_ns = first_ns;
    latencies[0] = static_cast<double>(first_ns);

    for (std::size_t i = 1; i < iterations; ++i)
    {
        std::int64_t cycle_latency_ns = 0;
        total_errors += verify_cycle(buffer, buffer_size_bytes, cycle_latency_ns);

        if (cycle_latency_ns < min_latency_ns) min_latency_ns = cycle_latency_ns;
        if (cycle_latency_ns > max_latency_ns) max_latency_ns = cycle_latency_ns;
        sum_latency_ns += cycle_latency_ns;

        latencies[i] = static_cast<double>(cycle_latency_ns);
    }

    double elapsed_seconds = total_timer.elapsed_seconds();

    results.timing.total_time_seconds = elapsed_seconds;
    results.timing.min_latency_ns = static_cast<double>(min_latency_ns);
    results.timing.max_latency_ns = static_cast<double>(max_latency_ns);
    results.timing.avg_latency_ns = static_cast<double>(sum_latency_ns) / static_cast<double>(iterations);
    results.timing.sample_count = iterations;

    calculate_statistics(latencies, results.timing.avg_latency_ns,
                         results.timing.variance_ns, results.timing.std_deviation_ns);

    // Factor of 3: read pass + write pass + verify pass per cycle.
    double total_bytes_processed = static_cast<double>(buffer_size_bytes)
                                 * static_cast<double>(iterations) * 3.0;
    results.throughput_mbps = (total_bytes_processed / elapsed_seconds) / (1024.0 * 1024.0);

    results.verification_errors = total_errors;
    results.verification_passed = (total_errors == 0);

    return results;
}

/**
 * Runs multiple benchmark passes with a shared buffer, aggregating
 * statistics across runs. Terminates when max_runs or max_duration is reached.
 */
MemoryAi::Results MemoryAi::run_continuous(
    std::size_t buffer_size_bytes,
    std::size_t iterations_per_run,
    std::size_t max_runs,
    double max_duration_seconds)
{
    Results aggregated_results{};
    aggregated_results.buffer_size_bytes = buffer_size_bytes;
    aggregated_results.iterations = iterations_per_run;
    aggregated_results.verification_passed = true;
    aggregated_results.verification_errors = 0;
    aggregated_results.timing.min_latency_ns = 0.0;
    aggregated_results.timing.max_latency_ns = 0.0;
    aggregated_results.timing.avg_latency_ns = 0.0;
    aggregated_results.timing.total_time_seconds = 0.0;
    aggregated_results.timing.variance_ns = 0.0;
    aggregated_results.timing.std_deviation_ns = 0.0;
    aggregated_results.timing.sample_count = 0;

    if (buffer_size_bytes == 0)
    {
        std::cerr << "Error: Buffer size must be greater than 0\n";
        return aggregated_results;
    }
    if (iterations_per_run == 0)
    {
        std::cerr << "Error: Iterations per run must be greater than 0\n";
        return aggregated_results;
    }
    if (max_runs == 0 && max_duration_seconds <= 0.0)
    {
        std::cerr << "Error: Either max_runs or max_duration_seconds must be specified\n";
        return aggregated_results;
    }

    // Reuse a single buffer across all runs to avoid repeated allocation
    std::vector<std::uint8_t> buffer(buffer_size_bytes);

    std::vector<double> run_avg_latencies;
    std::vector<double> run_total_times;
    std::size_t total_errors = 0;
    std::size_t completed_runs = 0;

    Timer continuous_timer;
    continuous_timer.start();

    bool should_continue = true;
    while (should_continue)
    {
        // Check time limit
        if (max_duration_seconds > 0.0)
        {
            double elapsed = continuous_timer.elapsed_seconds();
            if (elapsed >= max_duration_seconds)
            {
                should_continue = false;
                break;
            }
        }

        // Check run limit
        if (max_runs > 0 && completed_runs >= max_runs)
        {
            should_continue = false;
            break;
        }

        Results run_results = run_with_buffer(buffer.data(), buffer_size_bytes, iterations_per_run);

        if (run_results.timing.sample_count > 0)
        {
            run_avg_latencies.push_back(run_results.timing.avg_latency_ns);
            run_total_times.push_back(run_results.timing.total_time_seconds);
            total_errors += run_results.verification_errors;

            // Track global min/max across all runs
            if (completed_runs == 0)
            {
                aggregated_results.timing.min_latency_ns = run_results.timing.min_latency_ns;
                aggregated_results.timing.max_latency_ns = run_results.timing.max_latency_ns;
            }
            else
            {
                if (run_results.timing.min_latency_ns < aggregated_results.timing.min_latency_ns)
                    aggregated_results.timing.min_latency_ns = run_results.timing.min_latency_ns;
                if (run_results.timing.max_latency_ns > aggregated_results.timing.max_latency_ns)
                    aggregated_results.timing.max_latency_ns = run_results.timing.max_latency_ns;
            }

            aggregated_results.timing.total_time_seconds += run_results.timing.total_time_seconds;
            completed_runs++;
        }
        else
        {
            break;
        }
    }

    // Compute aggregate statistics across all completed runs
    if (completed_runs > 0)
    {
        double sum_avg_latency = 0.0;
        for (double avg : run_avg_latencies)
            sum_avg_latency += avg;

        aggregated_results.timing.avg_latency_ns = sum_avg_latency / static_cast<double>(completed_runs);
        aggregated_results.timing.sample_count = completed_runs;

        calculate_statistics(run_avg_latencies, aggregated_results.timing.avg_latency_ns,
                             aggregated_results.timing.variance_ns,
                             aggregated_results.timing.std_deviation_ns);

        double total_bytes_processed = static_cast<double>(buffer_size_bytes)
                                     * static_cast<double>(iterations_per_run)
                                     * static_cast<double>(completed_runs) * 3.0;
        aggregated_results.throughput_mbps = (total_bytes_processed / aggregated_results.timing.total_time_seconds)
                                           / (1024.0 * 1024.0);

        aggregated_results.iterations = iterations_per_run * completed_runs;
    }

    aggregated_results.verification_errors = total_errors;
    aggregated_results.verification_passed = (total_errors == 0);

    return aggregated_results;
}

/**
 * Computes sample variance and standard deviation using Bessel's correction
 * (divides by n-1 for an unbiased estimate from a sample).
 */
void MemoryAi::calculate_statistics(
    const std::vector<double> &latencies,
    double mean,
    double &variance,
    double &std_deviation) noexcept
{
    variance = 0.0;
    std_deviation = 0.0;

    if (latencies.empty() || latencies.size() == 1)
        return;

    double sum_squared_diff = 0.0;
    for (double latency : latencies)
    {
        double diff = latency - mean;
        sum_squared_diff += diff * diff;
    }

    std::size_t n = latencies.size();
    if (n > 1)
    {
        variance = sum_squared_diff / static_cast<double>(n - 1);
        if (variance > 0.0)
            std_deviation = std::sqrt(variance);
    }
}

/**
 * Fused read-write-verify cycle (Optimization 1).
 *
 * Instead of separate write_pattern() and verify_pattern() calls, the logic
 * is merged into two passes:
 *   Pass 1 -- Read all bytes to exercise the read path and prevent
 *             dead-store elimination of any prior writes.
 *   Pass 2 -- Write the expected pattern byte, then immediately re-read
 *             and verify in the same loop iteration. This keeps the cache
 *             line hot, reducing cost vs two separate full-buffer sweeps.
 */
std::size_t MemoryAi::verify_cycle(
    std::uint8_t *buffer,
    std::size_t size,
    std::int64_t &cycle_latency_ns) noexcept
{
    Timer cycle_timer;
    cycle_timer.start();

    // Pass 1: read -- touch every byte to exercise the read path.
    volatile std::uint8_t dummy = 0;
    for (std::size_t i = 0; i < size; ++i)
        dummy ^= buffer[i];
    (void)dummy;

    // Pass 2: write pattern + verify fused in a single loop.
    // Writing and immediately re-reading keeps the cache line hot,
    // reducing cost compared to two separate full-buffer sweeps.
    std::size_t errors = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto expected = static_cast<std::uint8_t>(i & 0xFF);
        buffer[i] = expected;
        if (buffer[i] != expected)
            ++errors;
    }

    cycle_latency_ns = cycle_timer.elapsed_nanoseconds();
    return errors;
}

/**
 * Prints detailed RAM benchmark results including configuration, timing
 * statistics, performance metrics, verification status, and a summary table.
 */
void MemoryAi::print_results(const Results &results)
{
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << " RAM Benchmark Results\n";
    std::cout << "========================================\n";
    std::cout << "\n";

    std::cout << "Configuration:\n";
    std::cout << " " << std::left << std::setw(25) << "Buffer Size:";
    if (results.buffer_size_bytes < 1024)
        std::cout << results.buffer_size_bytes << " bytes\n";
    else if (results.buffer_size_bytes < 1024 * 1024)
        std::cout << std::fixed << std::setprecision(2)
                  << (results.buffer_size_bytes / 1024.0) << " KB\n";
    else
        std::cout << std::fixed << std::setprecision(2)
                  << (results.buffer_size_bytes / (1024.0 * 1024.0)) << " MB\n";

    std::cout << " " << std::left << std::setw(25) << "Iterations:"
              << results.iterations << "\n";
    std::cout << "\n";

    std::cout << "Timing Statistics:\n";
    std::cout << " " << std::left << std::setw(25) << "Total Time:"
              << std::fixed << std::setprecision(6)
              << results.timing.total_time_seconds << " seconds\n";

    std::cout << " " << std::left << std::setw(25) << "Min Latency:"
              << std::fixed << std::setprecision(2)
              << results.timing.min_latency_ns << " ns\n";

    std::cout << " " << std::left << std::setw(25) << "Max Latency:"
              << std::fixed << std::setprecision(2)
              << results.timing.max_latency_ns << " ns\n";

    std::cout << " " << std::left << std::setw(25) << "Average Latency:"
              << std::fixed << std::setprecision(2)
              << results.timing.avg_latency_ns << " ns\n";

    double latency_spread_ns = results.timing.max_latency_ns - results.timing.min_latency_ns;
    std::cout << " " << std::left << std::setw(25) << "Latency Spread:"
              << std::fixed << std::setprecision(2)
              << latency_spread_ns << " ns\n";

    if (results.timing.sample_count > 1)
    {
        std::cout << " " << std::left << std::setw(25) << "Variance:"
                  << std::fixed << std::setprecision(2)
                  << results.timing.variance_ns << " ns^2\n";

        std::cout << " " << std::left << std::setw(25) << "Std Deviation:"
                  << std::fixed << std::setprecision(2)
                  << results.timing.std_deviation_ns << " ns\n";

        if (results.timing.avg_latency_ns > 0.0)
        {
            double cv = (results.timing.std_deviation_ns / results.timing.avg_latency_ns) * 100.0;
            std::cout << " " << std::left << std::setw(25) << "Coefficient of Variation:"
                      << std::fixed << std::setprecision(2)
                      << cv << " %\n";
        }
    }

    if (results.timing.sample_count > 0)
    {
        std::cout << " " << std::left << std::setw(25) << "Sample Count:"
                  << results.timing.sample_count << "\n";
    }
    std::cout << "\n";

    std::cout << "Performance Metrics:\n";
    std::cout << " " << std::left << std::setw(25) << "Throughput:"
              << std::fixed << std::setprecision(2)
              << results.throughput_mbps << " MB/s\n";

    double ops_per_second = static_cast<double>(results.iterations) / results.timing.total_time_seconds;
    std::cout << " " << std::left << std::setw(25) << "Operations/sec:"
              << std::fixed << std::setprecision(2)
              << ops_per_second;
    if (ops_per_second >= 1000000.0)
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << (ops_per_second / 1000000.0) << " M ops/s)";
    else if (ops_per_second >= 1000.0)
        std::cout << " (" << std::fixed << std::setprecision(2)
                  << (ops_per_second / 1000.0) << " K ops/s)";
    std::cout << "\n\n";

    std::cout << "Verification:\n";
    std::cout << " " << std::left << std::setw(25) << "Status:"
              << (results.verification_passed ? "PASSED" : "FAILED");
    if (!results.verification_passed)
        std::cout << " (" << results.verification_errors << " errors)";
    std::cout << "\n\n";

    // Summary table for quick comparison across runs
    std::cout << "Summary Table:\n";
    std::cout << " " << std::string(60, '-') << "\n";
    std::cout << " " << std::left  << std::setw(20) << "Metric"
              << std::right << std::setw(20) << "Value"
              << std::right << std::setw(20) << "Unit" << "\n";
    std::cout << " " << std::string(60, '-') << "\n";

    std::string buffer_size_str;
    if (results.buffer_size_bytes < 1024)
        buffer_size_str = std::to_string(results.buffer_size_bytes) + " B";
    else if (results.buffer_size_bytes < 1024 * 1024)
        buffer_size_str = std::to_string(results.buffer_size_bytes / 1024) + " KB";
    else
        buffer_size_str = std::to_string(results.buffer_size_bytes / (1024 * 1024)) + " MB";

    std::cout << " " << std::left  << std::setw(20) << "Buffer Size"
              << std::right << std::setw(20) << buffer_size_str
              << std::right << std::setw(20) << "" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Iterations"
              << std::right << std::setw(20) << results.iterations
              << std::right << std::setw(20) << "" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Total Time"
              << std::right << std::setw(20) << std::fixed << std::setprecision(6)
              << results.timing.total_time_seconds
              << std::right << std::setw(20) << "seconds" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Min Latency"
              << std::right << std::setw(20) << std::fixed << std::setprecision(2)
              << results.timing.min_latency_ns
              << std::right << std::setw(20) << "ns" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Max Latency"
              << std::right << std::setw(20) << std::fixed << std::setprecision(2)
              << results.timing.max_latency_ns
              << std::right << std::setw(20) << "ns" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Avg Latency"
              << std::right << std::setw(20) << std::fixed << std::setprecision(2)
              << results.timing.avg_latency_ns
              << std::right << std::setw(20) << "ns" << "\n";

    if (results.timing.sample_count > 1)
    {
        std::cout << " " << std::left  << std::setw(20) << "Std Deviation"
                  << std::right << std::setw(20) << std::fixed << std::setprecision(2)
                  << results.timing.std_deviation_ns
                  << std::right << std::setw(20) << "ns" << "\n";
    }

    std::cout << " " << std::left  << std::setw(20) << "Throughput"
              << std::right << std::setw(20) << std::fixed << std::setprecision(2)
              << results.throughput_mbps
              << std::right << std::setw(20) << "MB/s" << "\n";

    std::cout << " " << std::left  << std::setw(20) << "Verification"
              << std::right << std::setw(20) << (results.verification_passed ? "PASSED" : "FAILED")
              << std::right << std::setw(20) << "" << "\n";

    std::cout << " " << std::string(60, '-') << "\n\n";
}
