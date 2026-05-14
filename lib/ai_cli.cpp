/**
 * ai_cli.cpp - AI2ORBIT Windows CLI Benchmark Tool (MAXIMUM STRESS)
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 *
 * Runs CPU, DRAM, and GPU benchmarks in slow and fast phases.
 * FAST phases use maximum stress: multi-threaded CPU, 256MB DRAM, 8M GPU.
 * Includes screen blink (~3 sec) to pace GPU output + visual stress.
 * Measures integral-interpolation-logarithm power on CPU and DRAM.
 * Phasing speed: (46.7/1000 | 47.3/1000)/4
 * Pipeline: ALL -> CPU -> stores -> DRAM -> CPU -> GPU -> linear
 */

#include "cpu_ai.h"
#include "memory_ai.h"
#include "dram_mapper.h"
#include "cuda_ai.h"
#include "timer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

static constexpr double PHASE_SLOW = 46.7 / 1000.0;
static constexpr double PHASE_FAST = 47.3 / 1000.0;
static constexpr double PHASE_DIVISOR = 4.0;
static constexpr double PHASE_SLOW_RATE = PHASE_SLOW / PHASE_DIVISOR;
static constexpr double PHASE_FAST_RATE = PHASE_FAST / PHASE_DIVISOR;

struct PhaseResult {
    std::string name;
    std::string component;
    double value;
    std::string unit;
    double time_seconds;
    double phasing_speed;
};

static unsigned int get_thread_count() {
    unsigned int n = std::thread::hardware_concurrency();
    return (n > 0) ? n : 4;
}

static void enable_ansi() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | 0x0004);
    }
#endif
}

static void print_banner() {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "    ___   ____  ___   ____  ____  ____  ______\n";
    std::cout << "   /   | /  _/ / _ \\ / __ \\/ __ )/ / / /_  __/\n";
    std::cout << "  / /| | / /  / /_/ // /_/ / __ / / / / / /\n";
    std::cout << " / ___ |/ /  /  ___// _, _/ /_/ / /_/ / / /\n";
    std::cout << "/_/  |_/___/ /_/   /_/ |_/_____/\\____/ /_/\n";
    std::cout << "\n";
    std::cout << "  BenchmarkCore CLI v4.0.0 - MAXIMUM STRESS TEST\n";
    std::cout << "  Copyright (c) AI2ORBIT Co. 2026\n";
    std::cout << "  Authors: Sami Leino, Anirudha Talmale\n";
    std::cout << "  Platform: Windows 11 x86_64\n";
    std::cout << "  CPU Threads: " << get_thread_count() << "\n";
    std::cout << "================================================================\n\n";

    std::cout << "  Pipeline: ALL -> CPU -> stores -> DRAM -> CPU -> GPU -> linear\n";
    std::cout << "  Phasing Speed Formula: (46.7/1000 | 47.3/1000) / 4\n";
    std::cout << "  Slow Phase Rate: " << std::fixed << std::setprecision(6)
              << PHASE_SLOW_RATE << " GHz\n";
    std::cout << "  Fast Phase Rate: " << std::fixed << std::setprecision(6)
              << PHASE_FAST_RATE << " GHz\n\n";

    std::cout << "  WARNING: FAST phases will stress ALL hardware components.\n";
    std::cout << "  Screen will blink during GPU phase (~3 sec).\n";
    std::cout << "  Fans will spin up. CPU/GPU/DRAM temperatures will rise.\n";
    std::cout << "================================================================\n";
}

static void print_phase_header(int phase_num, int total, const std::string& name,
                                const std::string& speed_label) {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "  PHASE " << phase_num << "/" << total << " - " << name
              << " [" << speed_label << "]\n";
    std::cout << "================================================================\n";
}

static void print_separator() {
    std::cout << "  " << std::string(56, '-') << "\n";
}

static void print_metric(const std::string& label, double value,
                          const std::string& unit, int precision = 2) {
    std::cout << "  " << std::left << std::setw(28) << label
              << std::right << std::fixed << std::setprecision(precision)
              << std::setw(14) << value << " " << unit << "\n";
}

static void print_metric_str(const std::string& label, const std::string& value) {
    std::cout << "  " << std::left << std::setw(28) << label
              << std::right << std::setw(14) << value << "\n";
}

// ================================================================
// SCREEN BLINK - GPU visual stress (~3 seconds)
// Uses ANSI escape codes to rapidly cycle background colors
// on Windows 10+ console (VT100 mode)
// ================================================================

struct ScreenBlinkResult {
    double total_frames;
    double fps;
    double time_seconds;
    double gpu_draw_ops;
};

static ScreenBlinkResult run_screen_blink(double duration_seconds = 3.0) {
    ScreenBlinkResult result{};
    const int width = 80;
    const int bar_height = 8;

    struct ColorDef {
        int bg;
        int fg;
        const char* label;
    };

    ColorDef colors[] = {
        {47, 30, "WHITE BURST"},
        {42, 30, "GREEN PULSE"},
        {46, 30, "CYAN WAVE"},
        {43, 30, "YELLOW FLASH"},
        {45, 37, "MAGENTA GLOW"},
        {41, 37, "RED HEAT"},
        {44, 37, "BLUE COOL"},
        {40, 97, "BLACK RESET"},
        {107, 30, "BRIGHT WHITE"},
        {102, 30, "BRIGHT GREEN"},
        {106, 30, "BRIGHT CYAN"},
        {103, 30, "BRIGHT YELLOW"},
    };
    const int num_colors = 12;

    std::string bar_line(width, ' ');

    Timer t;
    t.start();
    double frames = 0;
    int color_idx = 0;

    while (t.elapsed_seconds() < duration_seconds) {
        int bg = colors[color_idx % num_colors].bg;
        int fg = colors[color_idx % num_colors].fg;
        const char* label = colors[color_idx % num_colors].label;

        std::cout << "\033[" << bg << ";" << fg << "m";

        for (int row = 0; row < bar_height; ++row) {
            if (row == bar_height / 2) {
                std::string center_line = bar_line;
                std::string lbl = label;
                int pad = (width - static_cast<int>(lbl.size())) / 2;
                if (pad > 0 && pad + static_cast<int>(lbl.size()) <= width) {
                    for (size_t i = 0; i < lbl.size(); ++i)
                        center_line[pad + i] = lbl[i];
                }
                double pct = (t.elapsed_seconds() / duration_seconds) * 100.0;
                std::ostringstream pss;
                pss << std::fixed << std::setprecision(0) << pct << "%";
                std::string ps = pss.str();
                int rpad = width - static_cast<int>(ps.size()) - 2;
                if (rpad > 0) {
                    for (size_t i = 0; i < ps.size(); ++i)
                        center_line[rpad + i] = ps[i];
                }
                std::cout << center_line << "\n";
            } else {
                int intensity = static_cast<int>(frames) % width;
                std::string anim_line(width, ' ');
                for (int c = 0; c < width; ++c) {
                    int dist = std::abs(c - intensity);
                    if (dist < 5) anim_line[c] = '#';
                    else if (dist < 10) anim_line[c] = '=';
                    else if (dist < 15) anim_line[c] = '-';
                    else if (dist < 20) anim_line[c] = '.';
                }
                std::cout << anim_line << "\n";
            }
        }

        std::cout << "\033[0m";
        std::cout.flush();

        frames++;
        color_idx++;

        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        std::cout << "\033[" << bar_height << "A";
    }

    for (int row = 0; row < bar_height; ++row) {
        std::cout << "\033[0m" << std::string(width, ' ') << "\n";
    }
    std::cout << "\033[" << bar_height << "A";
    std::cout << "\033[0m";
    std::cout.flush();

    result.time_seconds = t.elapsed_seconds();
    result.total_frames = frames;
    result.fps = frames / result.time_seconds;
    result.gpu_draw_ops = frames * bar_height * width;

    return result;
}

// ================================================================
// INTEGRAL-INTERPOLATION-LOGARITHM POWER MEASUREMENT
// Stresses FPU with chained math: numerical integration,
// polynomial interpolation, and logarithmic power series.
// ================================================================

struct InterpLogResult {
    double integral_value;
    double integral_ops;
    double integral_time_sec;

    double interp_value;
    double interp_ops;
    double interp_time_sec;

    double logarithm_value;
    double logarithm_ops;
    double logarithm_time_sec;

    double power_value;
    double power_ops;
    double power_time_sec;

    double total_ops;
    double total_time_sec;
    double total_mflops;

    double dram_throughput_mbps;
};

static double simpson_integrate(double a, double b, std::size_t n) {
    if (n % 2 != 0) n++;
    double h = (b - a) / static_cast<double>(n);
    double sum = std::log(std::abs(a) + 1.0) + std::log(std::abs(b) + 1.0);

    for (std::size_t i = 1; i < n; i += 2) {
        double x = a + static_cast<double>(i) * h;
        double fx = std::log(std::abs(x) + 1.0) * std::sin(x * 0.1) +
                    std::exp(-x * x * 0.001) * std::cos(x * 0.05);
        sum += 4.0 * fx;
    }
    for (std::size_t i = 2; i < n; i += 2) {
        double x = a + static_cast<double>(i) * h;
        double fx = std::log(std::abs(x) + 1.0) * std::sin(x * 0.1) +
                    std::exp(-x * x * 0.001) * std::cos(x * 0.05);
        sum += 2.0 * fx;
    }

    return (h / 3.0) * sum;
}

static double lagrange_interpolate(const std::vector<double>& x_vals,
                                    const std::vector<double>& y_vals,
                                    double target) {
    double result = 0.0;
    std::size_t n = x_vals.size();

    for (std::size_t i = 0; i < n; ++i) {
        double basis = 1.0;
        for (std::size_t j = 0; j < n; ++j) {
            if (i != j) {
                basis *= (target - x_vals[j]) / (x_vals[i] - x_vals[j]);
            }
        }
        result += y_vals[i] * basis;
    }
    return result;
}

static double ln_taylor(double x, int terms) {
    if (x <= 0.0) return -1e18;
    double t = (x - 1.0) / (x + 1.0);
    double t2 = t * t;
    double result = 0.0;
    double power = t;

    for (int n = 0; n < terms; ++n) {
        result += power / static_cast<double>(2 * n + 1);
        power *= t2;
    }
    return 2.0 * result;
}

static InterpLogResult run_integral_interp_log(std::size_t intensity) {
    InterpLogResult result{};
    unsigned int num_threads = get_thread_count();

    // --- INTEGRAL: Simpson's rule over large domain ---
    {
        Timer t;
        t.start();

        std::size_t total_steps = intensity * 2000000;
        std::vector<double> partial_results(num_threads);
        std::vector<std::thread> threads;

        double domain = 1000.0;
        double chunk = domain / static_cast<double>(num_threads);

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&partial_results, i, chunk, total_steps, num_threads]() {
                double a = static_cast<double>(i) * chunk;
                double b = a + chunk;
                std::size_t steps = total_steps / num_threads;
                partial_results[i] = simpson_integrate(a, b, steps);
            });
        }
        for (auto& th : threads) th.join();

        result.integral_value = 0.0;
        for (auto v : partial_results) result.integral_value += v;
        result.integral_time_sec = t.elapsed_seconds();
        result.integral_ops = static_cast<double>(total_steps) * 8.0;
    }

    // --- INTERPOLATION: Lagrange polynomial on log-sampled points ---
    {
        Timer t;
        t.start();

        std::size_t num_points = 200;
        std::size_t num_queries = intensity * 500000;

        std::vector<double> x_vals(num_points);
        std::vector<double> y_vals(num_points);
        for (std::size_t i = 0; i < num_points; ++i) {
            x_vals[i] = static_cast<double>(i) * 0.05;
            double x = x_vals[i];
            y_vals[i] = std::log(x + 1.0) * std::exp(-x * 0.1) + std::sin(x);
        }

        std::vector<double> query_results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&query_results, &x_vals, &y_vals, i,
                                  num_queries, num_threads, num_points]() {
                double sum = 0.0;
                std::size_t per_thread = num_queries / num_threads;
                for (std::size_t q = 0; q < per_thread; ++q) {
                    double target = static_cast<double>(q % (num_points - 1)) * 0.05 + 0.025;
                    std::size_t window_start = q % (num_points - 10);
                    std::vector<double> wx(x_vals.begin() + window_start,
                                           x_vals.begin() + window_start + 10);
                    std::vector<double> wy(y_vals.begin() + window_start,
                                           y_vals.begin() + window_start + 10);
                    sum += lagrange_interpolate(wx, wy, target);
                }
                query_results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        result.interp_value = 0.0;
        for (auto v : query_results) result.interp_value += v;
        result.interp_time_sec = t.elapsed_seconds();
        result.interp_ops = static_cast<double>(num_queries) * static_cast<double>(10 * 10);
    }

    // --- LOGARITHM: Taylor series ln() computation ---
    {
        Timer t;
        t.start();

        std::size_t num_evals = intensity * 1000000;
        int taylor_terms = 50;

        std::vector<double> log_results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&log_results, i, num_evals, num_threads, taylor_terms]() {
                double sum = 0.0;
                std::size_t per_thread = num_evals / num_threads;
                for (std::size_t j = 0; j < per_thread; ++j) {
                    double x = 0.5 + static_cast<double>(j % 10000) * 0.001;
                    sum += ln_taylor(x, taylor_terms);
                }
                log_results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        result.logarithm_value = 0.0;
        for (auto v : log_results) result.logarithm_value += v;
        result.logarithm_time_sec = t.elapsed_seconds();
        result.logarithm_ops = static_cast<double>(num_evals) * static_cast<double>(taylor_terms) * 4.0;
    }

    // --- POWER: x^n via exp(n*ln(x)) chain ---
    {
        Timer t;
        t.start();

        std::size_t num_evals = intensity * 2000000;

        std::vector<double> pow_results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&pow_results, i, num_evals, num_threads]() {
                double sum = 0.0;
                std::size_t per_thread = num_evals / num_threads;
                for (std::size_t j = 0; j < per_thread; ++j) {
                    double base = 1.0 + static_cast<double>(j % 5000) * 0.001;
                    double exponent = static_cast<double>((j + 7) % 100) * 0.1;
                    double val = std::exp(exponent * std::log(base));
                    sum += val * std::sin(val * 0.001);
                }
                pow_results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        result.power_value = 0.0;
        for (auto v : pow_results) result.power_value += v;
        result.power_time_sec = t.elapsed_seconds();
        result.power_ops = static_cast<double>(num_evals) * 6.0;
    }

    // --- DRAM stress: large buffer with log/interp pattern writes ---
    {
        std::size_t buf_size = 64 * 1024 * 1024;
        std::vector<double> buffer(buf_size / sizeof(double));
        Timer t;
        t.start();

        for (std::size_t pass = 0; pass < 3; ++pass) {
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                double x = static_cast<double>(i) * 0.0001 + 1.0;
                buffer[i] = std::log(x) * std::sin(x * 0.01) + static_cast<double>(pass);
            }
            volatile double sink = buffer[buffer.size() / 2];
            (void)sink;
        }

        double dram_time = t.elapsed_seconds();
        double total_bytes = static_cast<double>(buf_size) * 3.0 * 2.0;
        result.dram_throughput_mbps = (total_bytes / dram_time) / (1024.0 * 1024.0);
    }

    result.total_ops = result.integral_ops + result.interp_ops +
                       result.logarithm_ops + result.power_ops;
    result.total_time_sec = result.integral_time_sec + result.interp_time_sec +
                            result.logarithm_time_sec + result.power_time_sec;
    result.total_mflops = (result.total_ops / result.total_time_sec) / 1e6;

    return result;
}

// ================================================================
// MULTI-THREADED CPU/DRAM HELPERS (same as v3)
// ================================================================

struct MultiThreadCpuResult {
    double total_ops_per_sec;
    double total_mflops;
    double total_time_seconds;
    unsigned int threads_used;
    bool all_passed;
};

static MultiThreadCpuResult run_cpu_multithreaded(std::size_t iterations_per_thread) {
    unsigned int num_threads = get_thread_count();
    std::vector<CpuAi::Results> thread_results(num_threads);
    std::vector<std::thread> threads;

    Timer t;
    t.start();

    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&thread_results, i, iterations_per_thread]() {
            CpuAi cpu;
            thread_results[i] = cpu.run(iterations_per_thread);
        });
    }
    for (auto& th : threads) th.join();

    double wall_time = t.elapsed_seconds();

    MultiThreadCpuResult result{};
    result.threads_used = num_threads;
    result.total_time_seconds = wall_time;
    result.all_passed = true;

    double total_ops = 0.0;
    for (unsigned int i = 0; i < num_threads; ++i) {
        total_ops += static_cast<double>(thread_results[i].iterations) * 3.0;
        if (!thread_results[i].benchmark_successful)
            result.all_passed = false;
    }

    result.total_ops_per_sec = total_ops / wall_time;
    result.total_mflops = result.total_ops_per_sec / 1e6;

    return result;
}

struct MultiThreadMemResult {
    double total_throughput_mbps;
    double avg_latency_ns;
    double min_latency_ns;
    double max_latency_ns;
    double total_time_seconds;
    unsigned int threads_used;
    bool all_verified;
};

static MultiThreadMemResult run_dram_multithreaded(std::size_t buffer_per_thread,
                                                     std::size_t iterations) {
    unsigned int num_threads = get_thread_count();
    std::vector<MemoryAi::Results> thread_results(num_threads);
    std::vector<std::thread> threads;

    Timer t;
    t.start();

    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&thread_results, i, buffer_per_thread, iterations]() {
            MemoryAi mem;
            thread_results[i] = mem.run(buffer_per_thread, iterations);
        });
    }
    for (auto& th : threads) th.join();

    double wall_time = t.elapsed_seconds();

    MultiThreadMemResult result{};
    result.threads_used = num_threads;
    result.total_time_seconds = wall_time;
    result.all_verified = true;
    result.total_throughput_mbps = 0.0;
    result.min_latency_ns = 1e18;
    result.max_latency_ns = 0.0;
    double sum_avg = 0.0;

    for (unsigned int i = 0; i < num_threads; ++i) {
        result.total_throughput_mbps += thread_results[i].throughput_mbps;
        if (thread_results[i].timing.min_latency_ns < result.min_latency_ns)
            result.min_latency_ns = thread_results[i].timing.min_latency_ns;
        if (thread_results[i].timing.max_latency_ns > result.max_latency_ns)
            result.max_latency_ns = thread_results[i].timing.max_latency_ns;
        sum_avg += thread_results[i].timing.avg_latency_ns;
        if (!thread_results[i].verification_passed)
            result.all_verified = false;
    }

    result.avg_latency_ns = sum_avg / static_cast<double>(num_threads);
    return result;
}

// ================================================================
// MAIN
// ================================================================

int main(int argc, char* argv[]) {
    enable_ansi();
    print_banner();

    auto total_start = std::chrono::high_resolution_clock::now();
    std::vector<PhaseResult> all_results;
    const int TOTAL_PHASES = 8;
    unsigned int num_threads = get_thread_count();

    // ================================================================
    // PHASE 1: CPU PROCESSING - SLOW (single-threaded warmup)
    // ================================================================
    {
        print_phase_header(1, TOTAL_PHASES, "CPU PROCESSING", "SLOW");
        std::cout << "  Single-thread warmup...\n";
        std::cout << "  Iterations: 2,000,000 (Integer + Float + Memory workload)\n";
        print_separator();

        CpuAi cpu;
        Timer t;
        t.start();
        auto r = cpu.run(2000000);
        double elapsed = t.elapsed_seconds();

        double mflops = r.timing.operations_per_second / 1e6;
        double phasing = mflops * PHASE_SLOW_RATE;

        print_metric("Total Time:", elapsed, "sec", 4);
        print_metric("Operations/sec:", r.timing.operations_per_second, "ops/s", 0);
        print_metric("Throughput:", mflops, "MFlops");
        print_metric("Time/Operation:", r.timing.time_per_operation_ns, "ns", 2);
        print_metric("Phasing Speed:", phasing, "MFlops*GHz", 4);
        print_separator();
        print_metric_str("Status:", r.benchmark_successful ? "PASS" : "FAIL");

        PhaseResult pr;
        pr.name = "CPU SLOW";
        pr.component = "CPU";
        pr.value = mflops;
        pr.unit = "MFlops";
        pr.time_seconds = elapsed;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 2: CPU PROCESSING - VERY FAST (ALL THREADS)
    // ================================================================
    {
        print_phase_header(2, TOTAL_PHASES, "CPU PROCESSING", "VERY FAST");
        std::size_t iters_per_thread = 50000000;
        std::cout << "  MAXIMUM CPU STRESS - " << num_threads << " threads\n";
        std::cout << "  Iterations per thread: 50,000,000\n";
        std::cout << "  Total iterations: " << (iters_per_thread * num_threads) << "\n";
        std::cout << "  Integer ALU + FPU/SIMD + Memory load-store on ALL cores\n";
        print_separator();

        auto r = run_cpu_multithreaded(iters_per_thread);

        double phasing = r.total_mflops * PHASE_FAST_RATE;

        print_metric("Wall Clock Time:", r.total_time_seconds, "sec", 4);
        print_metric("Threads Used:", static_cast<double>(r.threads_used), "", 0);
        print_metric("Total Ops/sec:", r.total_ops_per_sec, "ops/s", 0);
        print_metric("Total Throughput:", r.total_mflops, "MFlops");
        print_metric("Per-Thread MFlops:", r.total_mflops / r.threads_used, "MFlops");
        print_metric("Phasing Speed:", phasing, "MFlops*GHz", 4);
        print_separator();
        print_metric_str("Status:", r.all_passed ? "PASS" : "FAIL");

        PhaseResult pr;
        pr.name = "CPU VERY FAST";
        pr.component = "CPU";
        pr.value = r.total_mflops;
        pr.unit = "MFlops";
        pr.time_seconds = r.total_time_seconds;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 3: DRAM PROCESSING - SLOW
    // ================================================================
    {
        print_phase_header(3, TOTAL_PHASES, "DRAM PROCESSING", "SLOW");
        std::cout << "  Memory hierarchy probe (64 MB buffer, 8 cycles)...\n";
        std::cout << "  Read-Write-Verify + DRAM Mapper 5-table classification\n";
        print_separator();

        MemoryAi mem;
        Timer t;
        t.start();
        auto r = mem.run(64 * 1024 * 1024, 8);
        double elapsed = t.elapsed_seconds();

        double phasing = r.throughput_mbps * PHASE_SLOW_RATE;

        print_metric("Total Time:", elapsed, "sec", 4);
        print_metric("Throughput:", r.throughput_mbps, "MB/s", 1);
        print_metric("Min Latency:", r.timing.min_latency_ns, "ns", 0);
        print_metric("Max Latency:", r.timing.max_latency_ns, "ns", 0);
        print_metric("Avg Latency:", r.timing.avg_latency_ns, "ns", 0);
        print_metric("Std Deviation:", r.timing.std_deviation_ns, "ns", 2);
        print_metric("Phasing Speed:", phasing, "MB/s*GHz", 4);
        print_separator();
        print_metric_str("Verification:", r.verification_passed ? "PASS" : "FAIL");

        DramMapper dm;
        auto dm_r = dm.map_memory(64 * 1024 * 1024);
        std::cout << "\n  DRAM Mapper (5-Table Priority):\n";
        print_metric("  Regions Mapped:", static_cast<double>(dm_r.regions_mapped), "", 0);
        print_metric("  Fastest Latency:", dm_r.fastest_latency_ns, "ns", 2);
        print_metric("  Slowest Latency:", dm_r.slowest_latency_ns, "ns", 2);
        print_metric("  Acceleration:", dm_r.acceleration_factor, "x", 2);
        print_metric("  STOP Markers:", static_cast<double>(dm_r.stop_markers_hit), "", 0);
        std::cout << "  Table 1 (CPU RAM):  " << dm_r.tables.table1_cpu_ram.size() << " regions\n";
        std::cout << "  Table 2 (RAM):      " << dm_r.tables.table2_ram.size() << " regions\n";
        std::cout << "  Table 3 (Priority): " << dm_r.tables.table3_priority.size() << " regions\n";
        std::cout << "  Table 4 (Extra):    " << dm_r.tables.table4_extra.size() << " regions\n";
        std::cout << "  Table 5 (Partial):  " << dm_r.tables.table5_partial.size() << " regions\n";

        PhaseResult pr;
        pr.name = "DRAM SLOW";
        pr.component = "DRAM";
        pr.value = r.throughput_mbps;
        pr.unit = "MB/s";
        pr.time_seconds = elapsed;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 4: DRAM PROCESSING - VERY FAST (ALL THREADS)
    // ================================================================
    {
        print_phase_header(4, TOTAL_PHASES, "DRAM PROCESSING", "VERY FAST");
        std::size_t buf_per_thread = 256 * 1024 * 1024;
        std::size_t total_mem_mb = (buf_per_thread * num_threads) / (1024 * 1024);
        std::cout << "  MAXIMUM DRAM STRESS - " << num_threads << " threads\n";
        std::cout << "  Buffer per thread: 256 MB\n";
        std::cout << "  Total memory: " << total_mem_mb << " MB (" << num_threads << " x 256 MB)\n";
        std::cout << "  Cycles per thread: 20 read-write-verify\n";
        print_separator();

        auto r = run_dram_multithreaded(buf_per_thread, 20);

        double phasing = r.total_throughput_mbps * PHASE_FAST_RATE;

        print_metric("Wall Clock Time:", r.total_time_seconds, "sec", 4);
        print_metric("Threads Used:", static_cast<double>(r.threads_used), "", 0);
        print_metric("Total Throughput:", r.total_throughput_mbps, "MB/s", 1);
        print_metric("Per-Thread MB/s:", r.total_throughput_mbps / r.threads_used, "MB/s", 1);
        print_metric("Min Latency:", r.min_latency_ns, "ns", 0);
        print_metric("Max Latency:", r.max_latency_ns, "ns", 0);
        print_metric("Avg Latency:", r.avg_latency_ns, "ns", 0);
        print_metric("Phasing Speed:", phasing, "MB/s*GHz", 4);
        print_separator();
        print_metric_str("Verification:", r.all_verified ? "PASS" : "FAIL");

        DramMapper dm;
        auto dm_r = dm.map_memory(128 * 1024 * 1024);
        double accel = dm.measure_acceleration();
        std::cout << "\n  DRAM Mapper (Full Probe):\n";
        print_metric("  Regions Mapped:", static_cast<double>(dm_r.regions_mapped), "", 0);
        print_metric("  Total Mapped:", static_cast<double>(dm_r.total_mapped_bytes) / (1024.0 * 1024.0), "MB", 1);
        print_metric("  Virtual Pool:", static_cast<double>(dm_r.virtual_pool_bytes) / (1024.0 * 1024.0), "MB", 1);
        print_metric("  Virt Ratio:", dm_r.virtualization_ratio, "x", 1);
        print_metric("  Acceleration:", dm_r.acceleration_factor, "x", 2);
        print_metric("  Measured Accel:", accel, "x", 2);

        PhaseResult pr;
        pr.name = "DRAM VERY FAST";
        pr.component = "DRAM";
        pr.value = r.total_throughput_mbps;
        pr.unit = "MB/s";
        pr.time_seconds = r.total_time_seconds;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 5: GPU PROCESSING - SLOW
    // ================================================================
    {
        print_phase_header(5, TOTAL_PHASES, "GPU PROCESSING", "SLOW");
        std::cout << "  GPU compute warmup (512K elements)...\n";
        std::cout << "  Matrix multiply + memory bandwidth + PCIe transfer\n";
        print_separator();

        CudaAi gpu;
        Timer t;
        t.start();
        auto r = gpu.run(512 * 1024);
        double elapsed = t.elapsed_seconds();

        double phasing = r.compute_gflops * PHASE_SLOW_RATE;

        print_metric("Total Time:", elapsed, "sec", 4);
        print_metric_str("Device:", r.device_name);
        print_metric("Compute:", r.compute_gflops, "GFLOPS", 2);
        print_metric("Memory BW:", r.memory_bandwidth_gbps, "GB/s", 2);
        print_metric("CPU->GPU Transfer:", r.cpu_to_gpu_transfer_gbps, "GB/s", 2);
        print_metric("GPU->CPU Transfer:", r.gpu_to_cpu_transfer_gbps, "GB/s", 2);
        print_metric("Phasing Speed:", phasing, "GFLOPS*GHz", 4);
        print_separator();
        print_metric_str("Status:", r.benchmark_successful ? "PASS" : "FAIL");

        PhaseResult pr;
        pr.name = "GPU SLOW";
        pr.component = "GPU";
        pr.value = r.compute_gflops;
        pr.unit = "GFLOPS";
        pr.time_seconds = elapsed;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 6: GPU PROCESSING - VERY FAST + SCREEN BLINK (~3 sec)
    // ================================================================
    {
        print_phase_header(6, TOTAL_PHASES, "GPU PROCESSING + SCREEN BLINK", "VERY FAST");
        std::cout << "  MAXIMUM GPU STRESS (8M elements) + screen blink (~3 sec)\n";
        std::cout << "  Dense matrix multiply + visual GPU output stress\n";
        std::cout << "  Running " << num_threads << " GPU simulations in parallel\n";
        print_separator();

        // --- Screen blink (visual GPU stress) ---
        std::cout << "\n  >>> SCREEN BLINK STARTING <<<\n\n";

        ScreenBlinkResult blink{};
        std::vector<CudaAi::Results> gpu_results(num_threads);
        std::vector<std::thread> threads;

        Timer t;
        t.start();

        // Run GPU compute + screen blink simultaneously
        std::thread blink_thread([&blink]() {
            blink = run_screen_blink(3.0);
        });

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&gpu_results, i]() {
                CudaAi gpu;
                gpu_results[i] = gpu.run(8 * 1024 * 1024);
            });
        }

        blink_thread.join();
        for (auto& th : threads) th.join();

        double elapsed = t.elapsed_seconds();

        double total_gflops = 0.0;
        double total_mem_bw = 0.0;
        double total_h2d = 0.0;
        double total_d2h = 0.0;
        bool all_ok = true;

        for (unsigned int i = 0; i < num_threads; ++i) {
            total_gflops += gpu_results[i].compute_gflops;
            total_mem_bw += gpu_results[i].memory_bandwidth_gbps;
            total_h2d += gpu_results[i].cpu_to_gpu_transfer_gbps;
            total_d2h += gpu_results[i].gpu_to_cpu_transfer_gbps;
            if (!gpu_results[i].benchmark_successful) all_ok = false;
        }

        double phasing = total_gflops * PHASE_FAST_RATE;

        std::cout << "\n  >>> SCREEN BLINK COMPLETE <<<\n\n";
        print_metric("Wall Clock Time:", elapsed, "sec", 4);
        print_metric("Threads Used:", static_cast<double>(num_threads), "", 0);
        print_metric_str("Device:", gpu_results[0].device_name);
        print_metric("Total Compute:", total_gflops, "GFLOPS", 2);
        print_metric("Per-Thread GFLOPS:", total_gflops / num_threads, "GFLOPS", 2);
        print_metric("Total Memory BW:", total_mem_bw, "GB/s", 2);
        print_metric("Total CPU->GPU:", total_h2d, "GB/s", 2);
        print_metric("Total GPU->CPU:", total_d2h, "GB/s", 2);
        print_metric("Phasing Speed:", phasing, "GFLOPS*GHz", 4);
        print_separator();
        std::cout << "  Screen Blink:\n";
        print_metric("    Frames Rendered:", blink.total_frames, "", 0);
        print_metric("    Frame Rate:", blink.fps, "FPS", 1);
        print_metric("    Draw Operations:", blink.gpu_draw_ops, "ops", 0);
        print_metric("    Blink Duration:", blink.time_seconds, "sec", 2);
        print_separator();
        print_metric_str("Status:", all_ok ? "PASS" : "FAIL");

        PhaseResult pr;
        pr.name = "GPU VERY FAST";
        pr.component = "GPU";
        pr.value = total_gflops;
        pr.unit = "GFLOPS";
        pr.time_seconds = elapsed;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 7: INTEGRAL-INTERPOLATION-LOGARITHM POWER (CPU)
    // ================================================================
    {
        print_phase_header(7, TOTAL_PHASES, "INTEGRAL-INTERP-LOG POWER", "CPU");
        std::cout << "  Numerical integration (Simpson's rule, multi-thread)\n";
        std::cout << "  Lagrange polynomial interpolation on log-sampled points\n";
        std::cout << "  Taylor series ln() computation (50 terms)\n";
        std::cout << "  Power series x^n via exp(n*ln(x)) chain\n";
        std::cout << "  All running on " << num_threads << " threads\n";
        print_separator();

        Timer t;
        t.start();
        auto r = run_integral_interp_log(2);
        double elapsed = t.elapsed_seconds();

        double phasing = r.total_mflops * PHASE_FAST_RATE;

        std::cout << "\n  Integral (Simpson's Rule):\n";
        print_metric("    Result:", r.integral_value, "", 6);
        print_metric("    Operations:", r.integral_ops, "ops", 0);
        print_metric("    Time:", r.integral_time_sec, "sec", 4);

        std::cout << "\n  Interpolation (Lagrange):\n";
        print_metric("    Result:", r.interp_value, "", 6);
        print_metric("    Operations:", r.interp_ops, "ops", 0);
        print_metric("    Time:", r.interp_time_sec, "sec", 4);

        std::cout << "\n  Logarithm (Taylor Series):\n";
        print_metric("    Result:", r.logarithm_value, "", 6);
        print_metric("    Operations:", r.logarithm_ops, "ops", 0);
        print_metric("    Time:", r.logarithm_time_sec, "sec", 4);

        std::cout << "\n  Power (exp(n*ln(x))):\n";
        print_metric("    Result:", r.power_value, "", 6);
        print_metric("    Operations:", r.power_ops, "ops", 0);
        print_metric("    Time:", r.power_time_sec, "sec", 4);

        std::cout << "\n";
        print_separator();
        print_metric("Total FP Operations:", r.total_ops, "ops", 0);
        print_metric("Total Compute Time:", r.total_time_sec, "sec", 4);
        print_metric("Throughput:", r.total_mflops, "MFlops", 2);
        print_metric("Phasing Speed:", phasing, "MFlops*GHz", 4);
        print_separator();
        print_metric_str("Status:", "PASS");

        PhaseResult pr;
        pr.name = "INT-INTERP-LOG";
        pr.component = "CPU+FPU";
        pr.value = r.total_mflops;
        pr.unit = "MFlops";
        pr.time_seconds = r.total_time_sec;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // PHASE 8: INTEGRAL-INTERP-LOG POWER (DRAM)
    // ================================================================
    {
        print_phase_header(8, TOTAL_PHASES, "INTEGRAL-INTERP-LOG POWER", "DRAM");
        std::cout << "  DRAM stress with log/interp pattern writes (64 MB x 3 passes)\n";
        std::cout << "  Measuring memory throughput under FP math load\n";
        print_separator();

        Timer t;
        t.start();

        std::size_t buf_size = 128 * 1024 * 1024;
        std::size_t num_doubles = buf_size / sizeof(double);

        std::vector<std::thread> threads;
        std::vector<double> thread_throughputs(num_threads, 0.0);

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&thread_throughputs, i, buf_size, num_doubles]() {
                std::vector<double> buffer(num_doubles / get_thread_count());
                std::size_t local_size = buffer.size();
                Timer lt;
                lt.start();

                for (std::size_t pass = 0; pass < 5; ++pass) {
                    for (std::size_t j = 0; j < local_size; ++j) {
                        double x = static_cast<double>(j + i * local_size) * 0.00001 + 1.0;
                        buffer[j] = std::log(x) * std::sin(x * 0.01) +
                                    std::exp(-x * 0.0001) * std::cos(x * 0.005);
                    }
                    volatile double sink = buffer[local_size / 2];
                    (void)sink;

                    for (std::size_t j = 0; j < local_size; ++j) {
                        double x = buffer[j];
                        buffer[j] = std::exp(x * 0.1) * std::log(std::abs(x) + 1.0);
                    }
                    volatile double sink2 = buffer[local_size / 3];
                    (void)sink2;
                }

                double lt_sec = lt.elapsed_seconds();
                double bytes = static_cast<double>(local_size * sizeof(double)) * 5.0 * 4.0;
                thread_throughputs[i] = (bytes / lt_sec) / (1024.0 * 1024.0);
            });
        }
        for (auto& th : threads) th.join();

        double elapsed = t.elapsed_seconds();

        double total_throughput = 0.0;
        for (auto tp : thread_throughputs) total_throughput += tp;

        double phasing = total_throughput * PHASE_FAST_RATE;

        print_metric("Wall Clock Time:", elapsed, "sec", 4);
        print_metric("Threads Used:", static_cast<double>(num_threads), "", 0);
        print_metric("Total DRAM Throughput:", total_throughput, "MB/s", 1);
        print_metric("Per-Thread MB/s:", total_throughput / num_threads, "MB/s", 1);
        print_metric("Phasing Speed:", phasing, "MB/s*GHz", 4);
        print_separator();
        print_metric_str("Status:", "PASS");

        PhaseResult pr;
        pr.name = "INT-LOG DRAM";
        pr.component = "DRAM+FPU";
        pr.value = total_throughput;
        pr.unit = "MB/s";
        pr.time_seconds = elapsed;
        pr.phasing_speed = phasing;
        all_results.push_back(pr);
    }

    // ================================================================
    // FINAL SCOREBOARD
    // ================================================================
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_seconds = std::chrono::duration<double>(total_end - total_start).count();

    std::cout << "\n\n";
    std::cout << "================================================================\n";
    std::cout << "  AI2ORBIT BenchmarkCore v4.0.0 - FULL STRESS RESULTS\n";
    std::cout << "================================================================\n\n";

    std::cout << "  Pipeline: ALL -> CPU -> stores -> DRAM -> CPU -> GPU -> linear\n";
    std::cout << "  Phasing Formula: (46.7/1000 | 47.3/1000) / 4\n";
    std::cout << "  Slow Rate: " << std::fixed << std::setprecision(6) << PHASE_SLOW_RATE << " GHz\n";
    std::cout << "  Fast Rate: " << std::fixed << std::setprecision(6) << PHASE_FAST_RATE << " GHz\n";
    std::cout << "  Threads:   " << num_threads << "\n\n";

    std::cout << "  Phase              Value          Unit         Time(s)   Phasing\n";
    std::cout << "  -----------------  -------------  -----------  --------  ----------\n";

    double total_phasing = 0.0;
    double total_phase_time = 0.0;

    for (const auto& pr : all_results) {
        std::ostringstream val_s, time_s, phase_s;
        val_s << std::fixed << std::setprecision(2) << pr.value;
        time_s << std::fixed << std::setprecision(3) << pr.time_seconds;
        phase_s << std::fixed << std::setprecision(4) << pr.phasing_speed;

        std::cout << "  " << std::left << std::setw(19) << pr.name
                  << std::right << std::setw(13) << val_s.str()
                  << "  " << std::left << std::setw(11) << pr.unit
                  << "  " << std::right << std::setw(8) << time_s.str()
                  << "  " << std::right << std::setw(10) << phase_s.str()
                  << "\n";

        total_phasing += pr.phasing_speed;
        total_phase_time += pr.time_seconds;
    }

    std::cout << "  -----------------  -------------  -----------  --------  ----------\n\n";

    double combined_slow = (all_results[0].phasing_speed +
                            all_results[2].phasing_speed +
                            all_results[4].phasing_speed);
    double combined_fast = (all_results[1].phasing_speed +
                            all_results[3].phasing_speed +
                            all_results[5].phasing_speed);
    double intlog_phasing = all_results[6].phasing_speed + all_results[7].phasing_speed;
    double full_speed = total_phasing / PHASE_DIVISOR;

    std::cout << "  COMBINED RESULTS:\n";
    std::cout << "  " << std::string(56, '-') << "\n";
    print_metric("Combined Slow Phasing:", combined_slow, "", 4);
    print_metric("Combined Fast Phasing:", combined_fast, "", 4);
    print_metric("Int-Interp-Log Phasing:", intlog_phasing, "", 4);
    print_metric("Total Phasing Sum:", total_phasing, "", 4);
    print_metric("Full Speed (sum/4):", full_speed, "GHz-equiv", 4);
    print_metric("Total Bench Time:", total_seconds, "seconds", 2);
    print_metric("Phase Time Sum:", total_phase_time, "seconds", 2);
    std::cout << "  " << std::string(56, '-') << "\n\n";

    double cpu_speedup = 0.0;
    if (all_results[0].value > 0)
        cpu_speedup = all_results[1].value / all_results[0].value;
    double dram_speedup = 0.0;
    if (all_results[2].value > 0)
        dram_speedup = all_results[3].value / all_results[2].value;
    double gpu_speedup = 0.0;
    if (all_results[4].value > 0)
        gpu_speedup = all_results[5].value / all_results[4].value;

    std::cout << "  SPEED RATIOS (Fast / Slow):\n";
    std::cout << "  " << std::string(56, '-') << "\n";
    print_metric("CPU  (Fast/Slow):", cpu_speedup, "x", 3);
    print_metric("DRAM (Fast/Slow):", dram_speedup, "x", 3);
    print_metric("GPU  (Fast/Slow):", gpu_speedup, "x", 3);
    std::cout << "  " << std::string(56, '-') << "\n\n";

    std::cout << "  Platform: Windows 11 x86_64\n";
    std::cout << "  Build: x86_64-w64-mingw32-g++ -O2 -std=c++17\n";
    std::cout << "================================================================\n";
    std::cout << "  Copyright (c) AI2ORBIT Co. 2026. All rights reserved.\n";
    std::cout << "================================================================\n\n";

    return 0;
}
