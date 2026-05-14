/**
 * timer.h - Timer class for high-precision timing
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 */

#ifndef TIMER_H
#define TIMER_H

#include <chrono>

/**
 * High-precision timer using std::chrono::high_resolution_clock.
 * Provides elapsed time in both seconds (double) and nanoseconds (long long)
 * for benchmark latency and throughput measurements.
 */
class Timer {
public:
    Timer() = default;

    /**
     * Resets the starting point to the current time.
     * Must be called before elapsed_seconds() or elapsed_nanoseconds().
     */
    void start() {
        m_start_time = std::chrono::high_resolution_clock::now();
    }

    /**
     * Returns the time elapsed since start() was called.
     * @return Elapsed time in fractional seconds (double precision).
     */
    double elapsed_seconds() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - m_start_time;
        return elapsed.count();
    }

    /**
     * Returns the time elapsed since start() was called in nanoseconds.
     * Useful for per-cycle latency tracking in memory benchmarks.
     * @return Elapsed time in nanoseconds (integer).
     */
    long long elapsed_nanoseconds() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - m_start_time
        ).count();
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time;
};

#endif // TIMER_H
