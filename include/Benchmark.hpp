#pragma once
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace nyc311 {

// ---------------------------------------------------------------------------
// Timer — wraps std::chrono for high-resolution wall-clock measurement
// ---------------------------------------------------------------------------
class Timer {
public:
    void start() { t0_ = Clock::now(); }
    void stop()  { t1_ = Clock::now(); }

    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(t1_ - t0_).count();
    }
    double elapsedSec() const { return elapsedMs() / 1000.0; }

private:
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point t0_, t1_;
};

// ---------------------------------------------------------------------------
// BenchmarkResult — statistics for N repeated runs of a timed block
// ---------------------------------------------------------------------------
struct BenchmarkResult {
    std::string label;
    double      mean_ms;
    double      min_ms;
    double      max_ms;
    double      stddev_ms;
    int         runs;
    size_t      result_count;   // number of records returned by the query

    void print() const {
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "[" << label << "]\n"
                  << "  runs        : " << runs        << "\n"
                  << "  mean (ms)   : " << mean_ms     << "\n"
                  << "  min  (ms)   : " << min_ms      << "\n"
                  << "  max  (ms)   : " << max_ms      << "\n"
                  << "  stddev (ms) : " << stddev_ms   << "\n"
                  << "  result count: " << result_count << "\n";
    }

    void writeCsv(std::ofstream& out) const {
        out << std::fixed << std::setprecision(3)
            << label      << ","
            << runs        << ","
            << mean_ms     << ","
            << min_ms      << ","
            << max_ms      << ","
            << stddev_ms   << ","
            << result_count << "\n";
    }
};

// ---------------------------------------------------------------------------
// Benchmark — runs a callable N times and returns statistics
// ---------------------------------------------------------------------------
class Benchmark {
public:
    static BenchmarkResult run(const std::string& label,
                               int                iterations,
                               std::function<size_t()> fn)
    {
        std::vector<double> times;
        times.reserve(static_cast<size_t>(iterations));
        size_t last_count = 0;
        Timer t;

        for (int i = 0; i < iterations; ++i) {
            t.start();
            last_count = fn();
            t.stop();
            times.push_back(t.elapsedMs());
        }

        double mean = std::accumulate(times.begin(), times.end(), 0.0)
                      / static_cast<double>(times.size());

        double mn = *std::min_element(times.begin(), times.end());
        double mx = *std::max_element(times.begin(), times.end());

        double var = 0.0;
        for (double v : times) {
            double d = v - mean;
            var += d * d;
        }
        var /= static_cast<double>(times.size());

        BenchmarkResult r;
        r.label        = label;
        r.mean_ms      = mean;
        r.min_ms       = mn;
        r.max_ms       = mx;
        r.stddev_ms    = std::sqrt(var);
        r.runs         = iterations;
        r.result_count = last_count;
        return r;
    }

    static void writeCsvHeader(std::ofstream& out) {
        out << "label,runs,mean_ms,min_ms,max_ms,stddev_ms,result_count\n";
    }
};

// ---------------------------------------------------------------------------
// MemoryStats — resident set size in KB (Linux /proc only)
// ---------------------------------------------------------------------------
struct MemoryStats {
    long rss_kb = 0;

    static MemoryStats capture() {
        MemoryStats s;
#ifdef __linux__
        struct rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        s.rss_kb = usage.ru_maxrss;   // already in KB on Linux
#endif
        return s;
    }

    void print(const std::string& tag = "") const {
        std::cout << "Memory RSS" << (tag.empty() ? "" : " [" + tag + "]")
                  << ": " << rss_kb << " KB  ("
                  << std::fixed << std::setprecision(1)
                  << static_cast<double>(rss_kb) / 1024.0 << " MB)\n";
    }
};

}  // namespace nyc311
