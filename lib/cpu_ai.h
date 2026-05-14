/**
 * cpu_ai.h - CPU benchmark with mixed workload testing
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 */

#ifndef CPU_AI_H
#define CPU_AI_H

#include <cstdint>
#include <string>

/**
 * CpuAi - Measures CPU computational throughput using a mixed workload
 * of integer arithmetic, floating-point transcendentals, and memory-bound ops.
 *
 * The three sub-workloads stress different execution units:
 *  - Integer: modular arithmetic + bitwise rotations (ALU/shift units)
 *  - Float:   fast sin/cos/sqrt/exp approximations (FPU/SIMD)
 *  - Memory:  small-array random access with dependencies (load-store unit)
 */
class CpuAi {
public:
    /**
     * Performance timing and throughput metrics for a benchmark run.
     */
    struct Timing {
        double total_time_seconds;      ///< Wall-clock time for the entire run
        double operations_per_second;   ///< Aggregate ops/sec across all workloads
        double time_per_operation_ns;   ///< Average nanoseconds per operation
    };

    /**
     * Final results of a CPU benchmark run.
     */
    struct Results {
        std::size_t iterations;         ///< Number of iterations executed
        std::string benchmark_type;     ///< Description of the workload mix
        bool benchmark_successful;      ///< True if the benchmark completed without error
        Timing timing;                  ///< Detailed timing metrics
    };

    CpuAi() noexcept;

    /**
     * Runs the mixed CPU workload (integer + float + memory-bound).
     * @param iterations Number of iterations for each sub-workload.
     * @return Results struct with timing and throughput metrics.
     */
    Results run(std::size_t iterations);

    /**
     * Prints formatted CPU benchmark results to stdout.
     * @param results The Results struct to display.
     */
    static void print_results(const Results& results);

private:
    /**
     * Integer sub-workload: modular arithmetic and bitwise rotate operations.
     * @param iterations Number of loop iterations.
     * @return Accumulated result (used to prevent dead-code elimination).
     */
    std::uint64_t compute_integer_workload(std::size_t iterations) noexcept;

    /**
     * Floating-point sub-workload: chained fast sin/cos/sqrt/exp approximations.
     * @param iterations Number of loop iterations.
     * @return Accumulated result (used to prevent dead-code elimination).
     */
    double compute_float_workload(std::size_t iterations) noexcept;

    /**
     * Memory-bound sub-workload: rapid read-modify-write on a 1024-element array
     * with a stride pattern that defeats simple prefetching.
     * @param iterations Number of loop iterations.
     * @return Accumulated result (used to prevent dead-code elimination).
     */
    std::uint64_t compute_memory_workload(std::size_t iterations) noexcept;
};

#endif // CPU_AI_H
