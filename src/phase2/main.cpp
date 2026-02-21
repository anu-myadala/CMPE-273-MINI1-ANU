// Phase 2 — OpenMP-parallelised AoS (Array-of-Structures)
// NYC 311 Service Requests dataset, 2020-present
//
// Build:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
// Run:    ./build/src/phase2/phase2  <csv_file> [csv_file2 ...] [--max N]
//
// Tip: Pass multiple yearly export files to exercise loadMultiple().

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "Benchmark.hpp"
#include "ParallelDataStore.hpp"
#include "Record311.hpp"

using namespace nyc311;

static void printSeparator() {
    std::cout << std::string(60, '-') << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: phase2 <csv_file> [csv_file2 ...]\n";
        return EXIT_FAILURE;
    }

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.emplace_back(argv[i]);

    // -----------------------------------------------------------------------
    // Thread count info
    // -----------------------------------------------------------------------
    std::cout << "=== Phase 2: OpenMP Parallel AoS ===\n";
    #ifdef _OPENMP
    std::cout << "OpenMP threads : " << omp_get_max_threads() << "\n";
    #else
    std::cout << "OpenMP         : NOT available (serial fallback)\n";
    #endif

    // -----------------------------------------------------------------------
    // 1. Load — multi-file parallel when more than one file is given
    // -----------------------------------------------------------------------
    auto mem_before = MemoryStats::capture();
    Timer load_timer;
    load_timer.start();

    ParallelDataStore store;
    try {
        if (files.size() == 1) {
            store.load(files[0]);
        } else {
            store.loadMultiple(files);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    load_timer.stop();
    auto mem_after = MemoryStats::capture();

    std::cout << "Records loaded : " << store.size() << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Load time (s)  : " << load_timer.elapsedSec() << "\n";
    std::cout << "Est. store size: "
              << store.memoryBytes() / (1024 * 1024) << " MB\n";
    std::cout << "RSS delta      : "
              << (mem_after.rss_kb - mem_before.rss_kb) / 1024 << " MB\n";
    printSeparator();

    // Borough distribution
    for (const auto& kv : store.countByBorough()) {
        std::cout << "  " << std::setw(15) << std::left << kv.first
                  << " : " << kv.second << "\n";
    }
    printSeparator();

    // -----------------------------------------------------------------------
    // 2. Benchmarks — same 5 queries as Phase 1 for direct comparison
    // -----------------------------------------------------------------------
    const int ITERATIONS = 12;
    std::vector<BenchmarkResult> results;

    results.push_back(Benchmark::run("Q1_borough_BROOKLYN", ITERATIONS, [&]() {
        return store.filterByBorough("BROOKLYN").size();
    }));

    results.push_back(Benchmark::run("Q2_complaint_Noise_Residential", ITERATIONS, [&]() {
        return store.filterByComplaintType("Noise - Residential").size();
    }));

    results.push_back(Benchmark::run("Q3_zip_range_Bronx", ITERATIONS, [&]() {
        return store.filterByZipRange(10451, 10475).size();
    }));

    results.push_back(Benchmark::run("Q4_geobox_Manhattan", ITERATIONS, [&]() {
        return store.filterByGeoBox(40.700, 40.882, -74.020, -73.907).size();
    }));

    results.push_back(Benchmark::run("Q5_date_range_2022", ITERATIONS, [&]() {
        return store.filterByDateRange(20220101, 20221231).size();
    }));

    printSeparator();
    for (const auto& r : results) r.print();

    const std::string csv_out = "phase2_results.csv";
    std::ofstream csv(csv_out);
    if (csv.is_open()) {
        Benchmark::writeCsvHeader(csv);
        for (const auto& r : results) r.writeCsv(csv);
        std::cout << "\nBenchmark results written to " << csv_out << "\n";
    }

    mem_after.print("final");
    return EXIT_SUCCESS;
}
