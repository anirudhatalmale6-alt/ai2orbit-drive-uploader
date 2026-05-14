/**
 * ai_cli_v2.cpp - AI2ORBIT CMD LINE V2
 * EQUALITY OF THE OINKERS
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 *
 * Pacing Constants:
 *   THIS     = 43.7/1000/4
 *   CAT      = 144.7/10000/3
 *   ADHESIVE = 197.9/1000000/9
 *
 * Moment 1 = THIS / CAT / ADHESIVE
 * Moment 1 Over = (35/360/37/35/16/1) / (360/16/35/36/39/1)
 *
 * RAM limiter: 0.0000000000008 seconds off sigma sigmoid softmax
 * rata = 0.003344558
 * LaPlace Y'' derivative chain
 */

#include "cpu_ai.h"
#include "memory_ai.h"
#include "dram_mapper.h"
#include "cuda_ai.h"
#include "timer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
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

// ================================================================
// PACING CONSTANTS - EQUALITY OF THE OINKERS
// ================================================================
static constexpr double THIS_PACE     = 43.7 / 1000.0 / 4.0;
static constexpr double CAT_PACE      = 144.7 / 10000.0 / 3.0;
static constexpr double ADHESIVE_PACE = 197.9 / 1000000.0 / 9.0;

static constexpr double MOMENT_1 = THIS_PACE / CAT_PACE / ADHESIVE_PACE;

// Moment 1 Over
static constexpr double M1_OVER_NUM = 35.0 / 360.0 / 37.0 / 35.0 / 16.0 / 1.0;
static constexpr double M1_OVER_DEN = 360.0 / 16.0 / 35.0 / 36.0 / 39.0 / 1.0;
static constexpr double MOMENT_1_OVER = M1_OVER_NUM / M1_OVER_DEN;

// RAM limiter sigma sigmoid
static constexpr double RAM_LIMITER = 0.0000000000008;
static constexpr double RATA = 0.003344558;

struct PhaseResult {
    std::string name;
    double value;
    std::string unit;
    double time_seconds;
    double pacing;
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

static void print_separator() {
    std::cout << "  " << std::string(60, '-') << "\n";
}

static void print_metric(const std::string& label, double value,
                          const std::string& unit, int precision = 2) {
    std::cout << "  " << std::left << std::setw(30) << label
              << std::right << std::fixed << std::setprecision(precision)
              << std::setw(16) << value << " " << unit << "\n";
}

static void print_metric_str(const std::string& label, const std::string& value) {
    std::cout << "  " << std::left << std::setw(30) << label
              << std::right << std::setw(16) << value << "\n";
}

static void print_metric_sci(const std::string& label, double value,
                              const std::string& unit) {
    std::cout << "  " << std::left << std::setw(30) << label
              << std::right << std::scientific << std::setprecision(10)
              << std::setw(22) << value << " " << unit << "\n";
}

// ================================================================
// SIGMA SIGMOID SOFTMAX - mathematical chain
// ================================================================

static double sigma(double x) {
    return 1.0 / (1.0 + std::exp(-x));
}

static double sigma_derivative(double x) {
    double s = sigma(x);
    return s * (1.0 - s);
}

static void softmax(const std::vector<double>& input, std::vector<double>& output) {
    double max_val = *std::max_element(input.begin(), input.end());
    double sum = 0.0;
    output.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = std::exp(input[i] - max_val);
        sum += output[i];
    }
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] /= sum;
    }
}

// LaPlace Y'' derivative chain
static double laplace_deriv_chain(double x, int order) {
    double result = x;
    for (int d = 0; d < order; ++d) {
        double h = 1e-6;
        double f_plus  = std::sin(result + h) * std::exp(-result * result * 0.01);
        double f_mid   = std::sin(result) * std::exp(-result * result * 0.01);
        double f_minus = std::sin(result - h) * std::exp(-result * result * 0.01);
        result = (f_plus - 2.0 * f_mid + f_minus) / (h * h);
        if (std::abs(result) > 1e15) result = std::fmod(result, 1000.0);
        if (std::isnan(result) || std::isinf(result)) result = 0.001;
    }
    return result;
}

// ================================================================
// MOMENT 1 - THIS / CAT / ADHESIVE phases
// ================================================================

struct MomentResult {
    double this_value;
    double this_time;
    double this_ops;

    double cat_value;
    double cat_time;
    double cat_ops;

    double adhesive_value;
    double adhesive_time;
    double adhesive_ops;

    double moment_result;
    double total_time;
    double total_ops;
};

static MomentResult run_moment_1() {
    MomentResult mr{};
    unsigned int num_threads = get_thread_count();

    // --- THIS phase: CPU structured coding at THIS_PACE ---
    // Sigma-sigmoid chain with LaPlace derivatives
    {
        Timer t;
        t.start();

        std::size_t iters = static_cast<std::size_t>(1.0 / THIS_PACE) * 1000;
        std::vector<double> results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&results, i, iters, num_threads]() {
                double sum = 0.0;
                std::size_t per_thread = iters / num_threads;
                for (std::size_t j = 0; j < per_thread; ++j) {
                    double x = static_cast<double>(j) * THIS_PACE;
                    double s = sigma(x) * sigma_derivative(x);
                    double lp = laplace_deriv_chain(x * 0.01, 3);
                    sum += s * lp * THIS_PACE;
                }
                results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        mr.this_value = 0.0;
        for (auto v : results) mr.this_value += v;
        mr.this_time = t.elapsed_seconds();
        mr.this_ops = static_cast<double>(iters) * 25.0;
    }

    // --- CAT phase: interpolation-logarithm at CAT_PACE ---
    {
        Timer t;
        t.start();

        std::size_t iters = static_cast<std::size_t>(1.0 / CAT_PACE) * 500;
        std::vector<double> results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&results, i, iters, num_threads]() {
                double sum = 0.0;
                std::size_t per_thread = iters / num_threads;
                for (std::size_t j = 0; j < per_thread; ++j) {
                    double x = static_cast<double>(j) * CAT_PACE + 1.0;
                    double ln_val = std::log(x);
                    double interp = ln_val * std::sin(x * 0.1) +
                                    std::exp(-x * 0.001) * std::cos(x * 0.05);
                    double power = std::exp(ln_val * CAT_PACE * 10.0);
                    sum += interp * power * CAT_PACE;
                }
                results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        mr.cat_value = 0.0;
        for (auto v : results) mr.cat_value += v;
        mr.cat_time = t.elapsed_seconds();
        mr.cat_ops = static_cast<double>(iters) * 20.0;
    }

    // --- ADHESIVE phase: softmax chain at ADHESIVE_PACE ---
    {
        Timer t;
        t.start();

        std::size_t iters = static_cast<std::size_t>(1.0 / ADHESIVE_PACE) * 2;
        std::vector<double> results(num_threads, 0.0);
        std::vector<std::thread> threads;

        for (unsigned int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&results, i, iters, num_threads]() {
                double sum = 0.0;
                std::size_t per_thread = iters / num_threads;
                std::vector<double> input(64);
                std::vector<double> output;

                for (std::size_t j = 0; j < per_thread; ++j) {
                    for (int k = 0; k < 64; ++k) {
                        input[k] = std::sin(static_cast<double>(j * 64 + k) *
                                   ADHESIVE_PACE) * 5.0;
                    }
                    softmax(input, output);
                    for (int k = 0; k < 64; ++k) {
                        sum += output[k] * sigma(input[k]);
                    }
                }
                results[i] = sum;
            });
        }
        for (auto& th : threads) th.join();

        mr.adhesive_value = 0.0;
        for (auto v : results) mr.adhesive_value += v;
        mr.adhesive_time = t.elapsed_seconds();
        mr.adhesive_ops = static_cast<double>(iters) * 64.0 * 6.0;
    }

    mr.moment_result = mr.this_value / (mr.cat_value != 0.0 ? mr.cat_value : 1.0) /
                        (mr.adhesive_value != 0.0 ? mr.adhesive_value : 1.0);
    mr.total_time = mr.this_time + mr.cat_time + mr.adhesive_time;
    mr.total_ops = mr.this_ops + mr.cat_ops + mr.adhesive_ops;

    return mr;
}

// ================================================================
// SLOW CPU PACER - deliberate slow-pace stress
// "SLO...W" mode: sustained low-frequency high-precision FP
// ================================================================

struct SlowPacerResult {
    double result;
    double time_seconds;
    double ops;
    double ram_offset_measured;
    double rata_computed;
    double laplace_result;
};

static SlowPacerResult run_slow_pacer() {
    SlowPacerResult sr{};
    unsigned int num_threads = get_thread_count();

    Timer t;
    t.start();

    std::size_t total_iters = 5000000;
    std::vector<double> thread_results(num_threads, 0.0);
    std::vector<double> thread_ram_offsets(num_threads, 0.0);
    std::vector<std::thread> threads;

    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&thread_results, &thread_ram_offsets, i,
                              total_iters, num_threads]() {
            double sum = 0.0;
            double ram_off = 0.0;
            std::size_t per_thread = total_iters / num_threads;

            std::vector<double> buffer(8192);

            for (std::size_t j = 0; j < per_thread; ++j) {
                double x = static_cast<double>(j) * 0.0001 + 0.001;

                // Sigma sigmoid chain
                double s1 = sigma(x);
                double s2 = sigma(s1 * 10.0 - 5.0);
                double s3 = sigma_derivative(s2 * 10.0 - 5.0);

                // Softmax single-step
                double soft_num = std::exp(s3 * 3.0);
                double soft_den = soft_num + std::exp(-s3) + std::exp(s3 * 0.5) + 1.0;
                double soft_val = soft_num / soft_den;

                // LaPlace derivative
                double lp = laplace_deriv_chain(x * 0.1, 2);

                // RAM write with measured offset
                std::size_t idx = j % 8192;
                auto before = std::chrono::high_resolution_clock::now();
                buffer[idx] = s1 * s2 * s3 * soft_val * lp;
                auto after = std::chrono::high_resolution_clock::now();
                double ns = std::chrono::duration<double>(after - before).count();
                ram_off += std::abs(ns - RAM_LIMITER);

                sum += buffer[idx] * RATA;
            }

            thread_results[i] = sum;
            thread_ram_offsets[i] = ram_off / static_cast<double>(per_thread);
        });
    }
    for (auto& th : threads) th.join();

    sr.result = 0.0;
    sr.ram_offset_measured = 0.0;
    for (unsigned int i = 0; i < num_threads; ++i) {
        sr.result += thread_results[i];
        sr.ram_offset_measured += thread_ram_offsets[i];
    }
    sr.ram_offset_measured /= static_cast<double>(num_threads);

    sr.time_seconds = t.elapsed_seconds();
    sr.ops = static_cast<double>(total_iters) * 30.0;
    sr.rata_computed = sr.result * RATA;
    sr.laplace_result = laplace_deriv_chain(sr.result * 0.001, 5);

    return sr;
}

// ================================================================
// SCREEN BLINK (~3 seconds)
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
    const int bar_height = 6;

    struct ColorDef { int bg; int fg; const char* label; };
    ColorDef colors[] = {
        {47, 30, "WHITE BURST"}, {42, 30, "GREEN PULSE"},
        {46, 30, "CYAN WAVE"}, {43, 30, "YELLOW FLASH"},
        {45, 37, "MAGENTA GLOW"}, {41, 37, "RED HEAT"},
        {44, 37, "BLUE COOL"}, {40, 97, "BLACK RESET"},
        {107, 30, "BRIGHT WHITE"}, {102, 30, "BRIGHT GREEN"},
        {106, 30, "BRIGHT CYAN"}, {103, 30, "BRIGHT YELLOW"},
    };

    Timer t;
    t.start();
    double frames = 0;
    int ci = 0;

    while (t.elapsed_seconds() < duration_seconds) {
        int bg = colors[ci % 12].bg;
        int fg = colors[ci % 12].fg;
        const char* label = colors[ci % 12].label;

        std::cout << "\033[" << bg << ";" << fg << "m";
        for (int row = 0; row < bar_height; ++row) {
            if (row == bar_height / 2) {
                std::string line(width, ' ');
                std::string lbl = label;
                int pad = (width - static_cast<int>(lbl.size())) / 2;
                if (pad > 0) for (size_t i = 0; i < lbl.size(); ++i) line[pad + i] = lbl[i];
                double pct = (t.elapsed_seconds() / duration_seconds) * 100.0;
                std::ostringstream ps; ps << std::fixed << std::setprecision(0) << pct << "%";
                std::string pstr = ps.str();
                int rp = width - static_cast<int>(pstr.size()) - 2;
                if (rp > 0) for (size_t i = 0; i < pstr.size(); ++i) line[rp + i] = pstr[i];
                std::cout << line << "\n";
            } else {
                std::string line(width, ' ');
                int pos = static_cast<int>(frames) % width;
                for (int c = 0; c < width; ++c) {
                    int d = std::abs(c - pos);
                    if (d < 4) line[c] = '#';
                    else if (d < 8) line[c] = '=';
                    else if (d < 12) line[c] = '-';
                }
                std::cout << line << "\n";
            }
        }
        std::cout << "\033[0m";
        std::cout.flush();
        frames++; ci++;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::cout << "\033[" << bar_height << "A";
    }

    for (int r = 0; r < bar_height; ++r)
        std::cout << "\033[0m" << std::string(width, ' ') << "\n";
    std::cout << "\033[" << bar_height << "A\033[0m";
    std::cout.flush();

    result.time_seconds = t.elapsed_seconds();
    result.total_frames = frames;
    result.fps = frames / result.time_seconds;
    result.gpu_draw_ops = frames * bar_height * width;
    return result;
}

// ================================================================
// MAIN
// ================================================================

int main(int argc, char* argv[]) {
    enable_ansi();

    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "    ___   ____  ___   ____  ____  ____  ______\n";
    std::cout << "   /   | /  _/ / _ \\ / __ \\/ __ )/ / / /_  __/\n";
    std::cout << "  / /| | / /  / /_/ // /_/ / __ / / / / / /\n";
    std::cout << " / ___ |/ /  /  ___// _, _/ /_/ / /_/ / / /\n";
    std::cout << "/_/  |_/___/ /_/   /_/ |_/_____/\\____/ /_/\n";
    std::cout << "\n";
    std::cout << "  BenchmarkCore CMD LINE V2\n";
    std::cout << "  EQUALITY OF THE OINKERS\n";
    std::cout << "  Copyright (c) AI2ORBIT Co. 2026\n";
    std::cout << "  Authors: Sami Leino, Anirudha Talmale\n";
    std::cout << "  Platform: Windows 11 x86_64\n";
    std::cout << "  CPU Threads: " << get_thread_count() << "\n";
    std::cout << "================================================================\n\n";

    std::cout << "  PACING CONSTANTS:\n";
    print_separator();
    print_metric_sci("  THIS  = 43.7/1000/4", THIS_PACE, "");
    print_metric_sci("  CAT   = 144.7/10000/3", CAT_PACE, "");
    print_metric_sci("  ADHESIVE = 197.9/1000000/9", ADHESIVE_PACE, "");
    print_metric_sci("  MOMENT 1 = THIS/CAT/ADHESIVE", MOMENT_1, "");
    std::cout << "\n";
    print_metric_sci("  M1 Over Num = 35/360/37/35/16/1", M1_OVER_NUM, "");
    print_metric_sci("  M1 Over Den = 360/16/35/36/39/1", M1_OVER_DEN, "");
    print_metric_sci("  MOMENT 1 OVER", MOMENT_1_OVER, "");
    std::cout << "\n";
    print_metric_sci("  RAM Limiter", RAM_LIMITER, "sec");
    print_metric_sci("  rata", RATA, "");
    print_separator();

    auto total_start = std::chrono::high_resolution_clock::now();
    std::vector<PhaseResult> all_results;

    // ================================================================
    // MOMENT 1: THIS / CAT / ADHESIVE
    // ================================================================
    {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  MOMENT 1: THIS / CAT / ADHESIVE\n";
        std::cout << "  CPU structured coding... coddddding..\n";
        std::cout << "================================================================\n";

        auto mr = run_moment_1();

        std::cout << "\n  THIS phase (sigma-sigmoid + LaPlace Y''):\n";
        print_metric("    Result:", mr.this_value, "", 10);
        print_metric("    Operations:", mr.this_ops, "ops", 0);
        print_metric("    Time:", mr.this_time, "sec", 4);
        print_metric("    Pacing:", THIS_PACE, "rate", 6);

        std::cout << "\n  CAT phase (interpolation-logarithm):\n";
        print_metric("    Result:", mr.cat_value, "", 10);
        print_metric("    Operations:", mr.cat_ops, "ops", 0);
        print_metric("    Time:", mr.cat_time, "sec", 4);
        print_metric("    Pacing:", CAT_PACE, "rate", 6);

        std::cout << "\n  ADHESIVE phase (softmax chain):\n";
        print_metric("    Result:", mr.adhesive_value, "", 10);
        print_metric("    Operations:", mr.adhesive_ops, "ops", 0);
        print_metric("    Time:", mr.adhesive_time, "sec", 4);
        print_metric("    Pacing:", ADHESIVE_PACE, "rate", 10);

        std::cout << "\n";
        print_separator();
        print_metric("  Moment 1 Result:", mr.moment_result, "", 10);
        print_metric("  Total Operations:", mr.total_ops, "ops", 0);
        print_metric("  Total Time:", mr.total_time, "sec", 4);
        double mflops = (mr.total_ops / mr.total_time) / 1e6;
        print_metric("  Throughput:", mflops, "MFlops", 2);
        print_separator();

        PhaseResult pr;
        pr.name = "MOMENT 1";
        pr.value = mflops;
        pr.unit = "MFlops";
        pr.time_seconds = mr.total_time;
        pr.pacing = mr.moment_result;
        all_results.push_back(pr);
    }

    // ================================================================
    // SLOW PACER - sigma sigmoid softmax + LaPlace Y'' + RAM limiter
    // ================================================================
    {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  SLO oooooooooooooooooooooooooooo W\n";
        std::cout << "  Slowest pacing of CPU pacers\n";
        std::cout << "  RAM limiter: 0.0000000000008 sec off sigma sigmoid\n";
        std::cout << "  softmaxers delight | rata = 0.003344558\n";
        std::cout << "  LaPlace Y'' derivative chain\n";
        std::cout << "================================================================\n";

        auto sr = run_slow_pacer();

        std::cout << "\n";
        print_metric("  Slow Pacer Result:", sr.result, "", 10);
        print_metric("  Operations:", sr.ops, "ops", 0);
        print_metric("  Time:", sr.time_seconds, "sec", 4);
        double mflops = (sr.ops / sr.time_seconds) / 1e6;
        print_metric("  Throughput:", mflops, "MFlops", 2);
        print_separator();
        print_metric_sci("  RAM Offset Measured:", sr.ram_offset_measured, "sec");
        print_metric_sci("  RAM Limiter Target:", RAM_LIMITER, "sec");
        print_metric("  rata * result:", sr.rata_computed, "", 10);
        print_metric("  LaPlace Y'' chain:", sr.laplace_result, "", 10);
        print_separator();

        PhaseResult pr;
        pr.name = "SLOW PACER";
        pr.value = mflops;
        pr.unit = "MFlops";
        pr.time_seconds = sr.time_seconds;
        pr.pacing = sr.rata_computed;
        all_results.push_back(pr);
    }

    // ================================================================
    // MOMENT 1 OVER - completion calculation
    // ================================================================
    {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  MOMENT 1 OVER\n";
        std::cout << "  (35/360/37/35/16/1) / (360/16/35/36/39/1)\n";
        std::cout << "================================================================\n";

        // Run full CPU benchmark at MOMENT_1_OVER pace
        Timer t;
        t.start();

        CpuAi cpu;
        std::size_t cpu_iters = static_cast<std::size_t>(1.0 / MOMENT_1_OVER) * 100;
        auto cpu_r = cpu.run(cpu_iters);

        // Run DRAM at MOMENT_1_OVER pace
        MemoryAi mem;
        auto mem_r = mem.run(64 * 1024 * 1024, 10);

        // Run DRAM mapper
        DramMapper dm;
        auto dm_r = dm.map_memory(64 * 1024 * 1024);
        double accel = dm.measure_acceleration();

        double elapsed = t.elapsed_seconds();

        double cpu_mflops = cpu_r.timing.operations_per_second / 1e6;

        std::cout << "\n  CPU at Moment 1 Over pace:\n";
        print_metric("    Iterations:", static_cast<double>(cpu_iters), "", 0);
        print_metric("    Throughput:", cpu_mflops, "MFlops", 2);
        print_metric("    Ops/sec:", cpu_r.timing.operations_per_second, "ops/s", 0);
        print_metric("    Time/Op:", cpu_r.timing.time_per_operation_ns, "ns", 2);

        std::cout << "\n  DRAM at Moment 1 Over pace:\n";
        print_metric("    Throughput:", mem_r.throughput_mbps, "MB/s", 1);
        print_metric("    Avg Latency:", mem_r.timing.avg_latency_ns, "ns", 0);
        print_metric("    Std Deviation:", mem_r.timing.std_deviation_ns, "ns", 2);
        print_metric_str("    Verification:", mem_r.verification_passed ? "PASS" : "FAIL");

        std::cout << "\n  DRAM Mapper:\n";
        print_metric("    Regions:", static_cast<double>(dm_r.regions_mapped), "", 0);
        print_metric("    Acceleration:", dm_r.acceleration_factor, "x", 2);
        print_metric("    Measured Accel:", accel, "x", 2);
        print_metric("    STOP Markers:", static_cast<double>(dm_r.stop_markers_hit), "", 0);

        std::cout << "\n";
        print_separator();
        print_metric_sci("  Moment 1 Over Value:", MOMENT_1_OVER, "");
        print_metric("  CPU Result:", cpu_mflops, "MFlops", 2);
        print_metric("  DRAM Result:", mem_r.throughput_mbps, "MB/s", 1);
        print_metric("  Total Time:", elapsed, "sec", 4);
        print_separator();

        PhaseResult pr;
        pr.name = "MOMENT 1 OVER";
        pr.value = cpu_mflops;
        pr.unit = "MFlops";
        pr.time_seconds = elapsed;
        pr.pacing = MOMENT_1_OVER;
        all_results.push_back(pr);
    }

    // ================================================================
    // GPU + SCREEN BLINK
    // ================================================================
    {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "  GPU PROCESSING + SCREEN BLINK (~3 sec)\n";
        std::cout << "================================================================\n";

        unsigned int num_threads = get_thread_count();
        std::cout << "\n  >>> SCREEN BLINK <<<\n\n";

        ScreenBlinkResult blink{};
        std::vector<CudaAi::Results> gpu_results(num_threads);
        std::vector<std::thread> threads;

        Timer t;
        t.start();

        std::thread blink_thread([&blink]() { blink = run_screen_blink(3.0); });
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
        for (unsigned int i = 0; i < num_threads; ++i)
            total_gflops += gpu_results[i].compute_gflops;

        std::cout << "\n  >>> BLINK COMPLETE <<<\n\n";
        print_metric("  Total Compute:", total_gflops, "GFLOPS", 2);
        print_metric("  Per-Thread:", total_gflops / num_threads, "GFLOPS", 2);
        print_metric("  Blink Frames:", blink.total_frames, "", 0);
        print_metric("  Blink FPS:", blink.fps, "FPS", 1);
        print_metric("  Draw Ops:", blink.gpu_draw_ops, "ops", 0);
        print_metric("  Time:", elapsed, "sec", 4);
        print_separator();

        PhaseResult pr;
        pr.name = "GPU + BLINK";
        pr.value = total_gflops;
        pr.unit = "GFLOPS";
        pr.time_seconds = elapsed;
        pr.pacing = total_gflops * THIS_PACE;
        all_results.push_back(pr);
    }

    // ================================================================
    // FINAL SCOREBOARD
    // ================================================================
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_seconds = std::chrono::duration<double>(total_end - total_start).count();

    std::cout << "\n\n";
    std::cout << "================================================================\n";
    std::cout << "  AI2ORBIT CMD LINE V2 - EQUALITY OF THE OINKERS\n";
    std::cout << "  FULL RESULTS\n";
    std::cout << "================================================================\n\n";

    std::cout << "  PACING CONSTANTS:\n";
    std::cout << "    THIS     = 43.7/1000/4     = " << std::fixed << std::setprecision(10)
              << THIS_PACE << "\n";
    std::cout << "    CAT      = 144.7/10000/3   = " << CAT_PACE << "\n";
    std::cout << "    ADHESIVE = 197.9/1000000/9 = " << ADHESIVE_PACE << "\n";
    std::cout << "    MOMENT 1 = " << std::scientific << MOMENT_1 << "\n";
    std::cout << "    MOMENT 1 OVER = " << MOMENT_1_OVER << "\n";
    std::cout << "    RAM Limiter = " << RAM_LIMITER << " sec\n";
    std::cout << "    rata = " << std::fixed << std::setprecision(10) << RATA << "\n\n";

    std::cout << "  Phase              Value          Unit         Time(s)   Pacing\n";
    std::cout << "  -----------------  -------------  -----------  --------  ----------\n";

    double total_pacing = 0.0;

    for (const auto& pr : all_results) {
        std::ostringstream val_s, time_s, pace_s;
        val_s << std::fixed << std::setprecision(2) << pr.value;
        time_s << std::fixed << std::setprecision(3) << pr.time_seconds;
        pace_s << std::scientific << std::setprecision(4) << pr.pacing;

        std::cout << "  " << std::left << std::setw(19) << pr.name
                  << std::right << std::setw(13) << val_s.str()
                  << "  " << std::left << std::setw(11) << pr.unit
                  << "  " << std::right << std::setw(8) << time_s.str()
                  << "  " << std::right << std::setw(10) << pace_s.str()
                  << "\n";

        total_pacing += pr.pacing;
    }

    std::cout << "  -----------------  -------------  -----------  --------  ----------\n\n";

    std::cout << "  EQUALITY RESULT:\n";
    print_separator();
    print_metric_sci("  Total Pacing Sum:", total_pacing, "");
    print_metric_sci("  Moment 1:", MOMENT_1, "");
    print_metric_sci("  Moment 1 Over:", MOMENT_1_OVER, "");
    print_metric_sci("  RAM Limiter:", RAM_LIMITER, "sec");
    print_metric("  rata:", RATA, "", 10);
    print_metric("  Total Bench Time:", total_seconds, "seconds", 2);
    print_separator();

    std::cout << "\n  Platform: Windows 11 x86_64\n";
    std::cout << "================================================================\n";
    std::cout << "  Copyright (c) AI2ORBIT Co. 2026. All rights reserved.\n";
    std::cout << "================================================================\n\n";

    return 0;
}
