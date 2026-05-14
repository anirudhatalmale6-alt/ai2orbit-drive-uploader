/**
 * dram_mapper.h - DRAM memory mapper with 5-table RAM prioritization
 *
 * Copyright (c) AI2ORBIT Co. 2026
 * Authors: Sami Leino, Anirudha Talmale
 * All rights reserved.
 *
 * This software is proprietary and confidential.
 * Unauthorized copying, distribution, or modification is strictly prohibited.
 */

#ifndef DRAM_MAPPER_H
#define DRAM_MAPPER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * DramMapper - Extended memory profiler with 5-priority RAM tables
 * and a virtual memory pool that routes data to the fastest regions.
 *
 * Profiles allocatable memory regions, classifies them into 5 priority
 * tables (Table 1 = fastest CPU RAM, Table 5 = slowest/partial), and
 * provides prioritized allocation from a virtual pool sized at 4.2x
 * the physical mapped memory.
 */
class DramMapper {
public:
    /** Represents one mapped memory region with its measured performance. */
    struct MemoryRegion {
        void* base_address;          ///< Start address of the allocated block
        std::size_t size_bytes;      ///< Size of the region in bytes
        double read_latency_ns;      ///< Measured per-byte read latency in ns
        double write_latency_ns;     ///< Measured per-byte write latency in ns
        double bandwidth_mbps;       ///< Sequential read bandwidth in MB/s
        int priority_table;          ///< Assigned table 1-5 (1=fastest, 5=slowest)
        std::string label;           ///< Human-readable label (CPU_RAM, RAM, etc.)
    };

    /** The 5 RAM priority tables from the AI2ORBIT specification. */
    struct RamTables {
        std::vector<MemoryRegion> table1_cpu_ram;    ///< Table 1: USE OF CPU RAM (fastest)
        std::vector<MemoryRegion> table2_ram;         ///< Table 2: USE OF RAM
        std::vector<MemoryRegion> table3_priority;    ///< Table 3: USE OF RAM PRIORITY
        std::vector<MemoryRegion> table4_extra;       ///< Table 4: USE OF EXTRA RAM
        std::vector<MemoryRegion> table5_partial;     ///< Table 5: PARTIAL USE (slowest)
    };

    /** Aggregated results from a full memory mapping pass. */
    struct Results {
        RamTables tables;                   ///< Regions sorted into the 5 priority tables
        std::size_t total_mapped_bytes;     ///< Total bytes of physical memory probed
        std::size_t virtual_pool_bytes;     ///< Virtual pool size (4.2x physical mapped)
        double virtualization_ratio;        ///< Actual ratio (should be ~4.2)
        std::size_t regions_mapped;         ///< Number of distinct regions probed
        double fastest_latency_ns;          ///< Best latency observed across all regions
        double slowest_latency_ns;          ///< Worst latency observed across all regions
        double acceleration_factor;         ///< Estimated speedup from prioritized allocation
        bool mapping_successful;            ///< True if at least one region was mapped

        std::size_t total_read_cycles;      ///< Total read operations performed during probing
        std::size_t total_write_cycles;     ///< Total write operations performed during probing
        std::size_t stop_markers_hit;       ///< Number of STOP boundaries (latency jumps) detected
    };

    DramMapper() noexcept;

    /**
     * Profiles all allocatable memory and sorts regions into 5 priority tables.
     * Probes blocks from 4KB up to max_probe_bytes, doubling each time.
     * @param max_probe_bytes Maximum total bytes to probe (default 128MB).
     * @return Results with table assignments, latency stats, and pool metrics.
     */
    Results map_memory(std::size_t max_probe_bytes = 128 * 1024 * 1024);

    /**
     * Allocates from the virtual pool, preferring the fastest available table.
     * Falls back to malloc if no suitable pool entry exists.
     * @param size_bytes Number of bytes to allocate.
     * @return Pointer to the allocated memory, or nullptr on failure.
     */
    void* allocate_prioritized(std::size_t size_bytes);

    /**
     * Frees a previously prioritized allocation (returns it to the pool).
     * @param ptr Pointer obtained from allocate_prioritized().
     */
    void free_prioritized(void* ptr);

    /**
     * Measures the acceleration factor of prioritized vs standard malloc.
     * Runs identical workloads on both paths and returns the time ratio.
     * @return Speedup ratio (>1.0 means prioritized is faster).
     */
    double measure_acceleration();

    /**
     * Formats mapping results as a human-readable string with table breakdown.
     * @param results The Results struct to format.
     * @return Formatted multi-line string.
     */
    static std::string format_results(const Results& results);

private:
    /**
     * Probes a memory region with write-read-verify cycles to measure performance.
     * @param base Pointer to the start of the region.
     * @param size Size of the region in bytes.
     * @return MemoryRegion with measured latency and bandwidth.
     */
    MemoryRegion probe_region(void* base, std::size_t size);

    /**
     * Classifies a region into table 1-5 based on its latency relative
     * to the fastest and slowest observed latencies.
     * @param region  The region to classify.
     * @param fastest Fastest average latency across all regions.
     * @param slowest Slowest average latency across all regions.
     * @return Priority table number (1=fastest, 5=slowest).
     */
    int classify_region(const MemoryRegion& region, double fastest, double slowest);

    /**
     * Detects STOP boundaries where latency jumps > 1.5x between adjacent regions.
     * @param regions Vector of probed regions in allocation order.
     * @return Number of STOP markers detected.
     */
    std::size_t detect_stop_markers(const std::vector<MemoryRegion>& regions);

    /** Internal pool entry tracking allocated blocks and their table assignment. */
    struct PoolEntry {
        void* ptr;         ///< Pointer to the allocated block
        std::size_t size;  ///< Size of the block in bytes
        int table;         ///< Priority table (1-5) this block belongs to
        bool in_use;       ///< True if currently allocated to a caller
    };
    std::vector<PoolEntry> pool_;   ///< Pool of mapped memory blocks
    Results last_results_;          ///< Cached results from the most recent map_memory() call
    bool has_mapped_;               ///< True after map_memory() has been called at least once
};

#endif // DRAM_MAPPER_H
