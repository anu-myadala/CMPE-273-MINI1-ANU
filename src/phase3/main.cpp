// Phase 3 — Vectorized Structure-of-Arrays (SoA) with OpenMP
// NYC 311 Service Requests dataset, 2020-present
//
// Build:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
// Run:    ./build/src/phase3/phase3  <csv_file> [csv_file2 ...]
//
// The -fopt-info-vec flag (set in CMakeLists.txt) prints which loops the
// compiler successfully auto-vectorised — use to confirm SIMD is active.

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "Benchmark.hpp"
#include "Record311.hpp"
#include "VectorStore.hpp"

using namespace nyc311;

static void printSeparator() {
    std::cout << std::string(60, '-') << "\n";
}

// ---------------------------------------------------------------------------
// Extra SoA-specific analytics that would be expensive in AoS
// ---------------------------------------------------------------------------

// Mean latitude/longitude using SIMD-friendly reduction over contiguous arrays
static void printCentroid(const VectorStore& store) {
    const size_t n = store.size();
    double sum_lat = 0.0;
    double sum_lon = 0.0;
    size_t valid   = 0;

    // This loop operates on two independent contiguous double[] arrays.
    // With -O3 -march=native, gcc/clang auto-vectorises with AVX2.
    #pragma omp parallel for reduction(+:sum_lat,sum_lon,valid) schedule(static)
    for (size_t i = 0; i < n; ++i) {
        double la = store.latitudes[i];
        double lo = store.longitudes[i];
        if (la != 0.0 && lo != 0.0) {
            sum_lat += la;
            sum_lon += lo;
            ++valid;
        }
    }

    if (valid > 0) {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Dataset centroid  : ("
                  << sum_lat / static_cast<double>(valid) << ", "
                  << sum_lon / static_cast<double>(valid) << ")\n";
        std::cout << "Geo-valid records : " << valid << " / " << n << "\n";
    }
}

// Count open vs closed tickets — single pass over the status[] string vector
static void printStatusSplit(const VectorStore& store) {
    size_t open_count = 0, closed_count = 0;
    for (const auto& s : store.statuses) {
        if (s == "Open")        ++open_count;
        else if (s == "Closed") ++closed_count;
    }
    std::cout << "Open tickets   : " << open_count  << "\n";
    std::cout << "Closed tickets : " << closed_count << "\n";
}

// Channel breakdown (PHONE / ONLINE / MOBILE / OTHER)
static void printChannelSplit(const VectorStore& store) {
    std::map<std::string, size_t> counts;
    for (const auto& c : store.channel_types) counts[c]++;
    std::cout << "Channel types:\n";
    for (const auto& kv : counts) {
        std::cout << "  " << std::setw(10) << std::left << kv.first
                  << " : " << kv.second << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: phase3 <csv_file> [csv_file2 ...]\n";
        return EXIT_FAILURE;
    }

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.emplace_back(argv[i]);

    std::cout << "=== Phase 3: SoA Vectorized ===\n";
    #ifdef _OPENMP
    std::cout << "OpenMP threads : " << omp_get_max_threads() << "\n";
    #else
    std::cout << "OpenMP         : NOT available\n";
    #endif

    // -----------------------------------------------------------------------
    // 1. Load
    // -----------------------------------------------------------------------
    auto mem_before = MemoryStats::capture();
    Timer load_timer;
    load_timer.start();

    VectorStore store;
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

    printCentroid(store);
    printStatusSplit(store);
    printChannelSplit(store);
    printSeparator();

    // -----------------------------------------------------------------------
    // 2. Benchmarks — identical query set for apples-to-apples comparison
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

    // Q4: geo box — this query most benefits from SoA (only lat/lon vectors touched)
    results.push_back(Benchmark::run("Q4_geobox_Manhattan", ITERATIONS, [&]() {
        return store.filterByGeoBox(40.700, 40.882, -74.020, -73.907).size();
    }));

    results.push_back(Benchmark::run("Q5_date_range_2022", ITERATIONS, [&]() {
        return store.filterByDateRange(20220101, 20221231).size();
    }));

    // Q6: centroid computation — pure numeric reduction, SIMD showcase
    results.push_back(Benchmark::run("Q6_centroid_reduction", ITERATIONS, [&]() {
        double s = 0.0;
        const size_t n = store.latitudes.size();
        #pragma omp parallel for reduction(+:s) schedule(static)
        for (size_t i = 0; i < n; ++i) s += store.latitudes[i];
        return static_cast<size_t>(s != 0.0);  // non-zero → 1 result
    }));

    printSeparator();
    for (const auto& r : results) r.print();

    const std::string csv_out = "phase3_results.csv";
    std::ofstream csv(csv_out);
    if (csv.is_open()) {
        Benchmark::writeCsvHeader(csv);
        for (const auto& r : results) r.writeCsv(csv);
        std::cout << "\nBenchmark results written to " << csv_out << "\n";
    }

    mem_after.print("final");
    return EXIT_SUCCESS;
}
