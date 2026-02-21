// Phase 1 — Serial AoS (Array-of-Structures) baseline
// NYC 311 Service Requests dataset, 2020-present
//
// Build:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
// Run:    ./build/src/phase1/phase1  <csv_file> [csv_file2 ...]

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "Benchmark.hpp"
#include "DataStore.hpp"
#include "Record311.hpp"

using namespace nyc311;

static void printSeparator() {
    std::cout << std::string(60, '-') << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: phase1 <csv_file> [csv_file2 ...]\n";
        return EXIT_FAILURE;
    }

    // Collect all file arguments
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        files.emplace_back(argv[i]);
    }

    // -----------------------------------------------------------------------
    // 1. Load data (serial, one file at a time)
    // -----------------------------------------------------------------------
    std::cout << "=== Phase 1: Serial AoS ===\n";
    std::cout << "Loading " << files.size() << " file(s)...\n";

    auto mem_before = MemoryStats::capture();

    Timer load_timer;
    load_timer.start();

    DataStore store;
    try {
        for (const auto& filepath : files) {
            std::cout << "  Loading: " << filepath << "\n";
            store.load(filepath);
        }
    }
    catch (const std::exception& e) {
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

    // -----------------------------------------------------------------------
    // 2. Borough distribution (diagnostic)
    // -----------------------------------------------------------------------
    std::cout << "Borough distribution:\n";
    auto borough_counts = store.countByBorough();
    for (const auto& kv : borough_counts) {
        std::cout << "  " << std::setw(15) << std::left << kv.first
                  << " : " << kv.second << "\n";
    }
    printSeparator();

    std::cout << "Top-10 complaint types:\n";
    auto type_counts = store.countByComplaintType(10);
    for (const auto& kv : type_counts) {
        std::cout << "  " << std::setw(40) << std::left << kv.first
                  << " : " << kv.second << "\n";
    }
    printSeparator();

    // -----------------------------------------------------------------------
    // 3. Benchmarks — 12 iterations each (warm cache after first run)
    // -----------------------------------------------------------------------
    const int ITERATIONS = 12;
    std::vector<BenchmarkResult> results;

    // Q1: Filter by borough — BROOKLYN
    results.push_back(Benchmark::run("Q1_borough_BROOKLYN", ITERATIONS, [&]() {
        return store.filterByBorough("BROOKLYN").size();
    }));

    // Q2: Filter by complaint type — most common type (usually "Noise - Residential")
    results.push_back(Benchmark::run("Q2_complaint_Noise_Residential", ITERATIONS, [&]() {
        return store.filterByComplaintType("Noise - Residential").size();
    }));

    // Q3: Zip-code range — Bronx zip codes [10451, 10475]
    results.push_back(Benchmark::run("Q3_zip_range_Bronx", ITERATIONS, [&]() {
        return store.filterByZipRange(10451, 10475).size();
    }));

    // Q4: Geographic bounding box — Manhattan island (approx.)
    results.push_back(Benchmark::run("Q4_geobox_Manhattan", ITERATIONS, [&]() {
        return store.filterByGeoBox(40.700, 40.882, -74.020, -73.907).size();
    }));

    // Q5: Date range — calendar year 2022
    results.push_back(Benchmark::run("Q5_date_range_2022", ITERATIONS, [&]() {
        return store.filterByDateRange(20220101, 20221231).size();
    }));

    // -----------------------------------------------------------------------
    // 4. Print results
    // -----------------------------------------------------------------------
    printSeparator();
    for (const auto& r : results) r.print();

    // -----------------------------------------------------------------------
    // 5. Write CSV for Python plotter
    // -----------------------------------------------------------------------
    const std::string csv_out = "phase1_results.csv";
    std::ofstream csv(csv_out);
    if (csv.is_open()) {
        Benchmark::writeCsvHeader(csv);
        for (const auto& r : results) r.writeCsv(csv);
        std::cout << "\nBenchmark results written to " << csv_out << "\n";
    }

    mem_after.print("final");
    return EXIT_SUCCESS;
}
