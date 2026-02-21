#pragma once
#include <algorithm>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "CSVParser.hpp"
#include "IDataStore.hpp"
#include "Record311.hpp"

namespace nyc311 {

// ---------------------------------------------------------------------------
// ParallelDataStore  —  Phase 2: OpenMP-parallelised AoS container.
//
// Data layout is identical to DataStore (Array-of-Structures).  The
// difference is that every query loop is parallelised with OpenMP.
// Loading is also tiled across multiple files when more than one path is
// supplied, allowing file-level I/O parallelism.
// ---------------------------------------------------------------------------
class ParallelDataStore : public IDataStore {
public:
    // -----------------------------------------------------------------------
    // load — serial file read (I/O is the bottleneck; parallel CSV parsing
    //        is handled via loadMultiple for multi-file datasets).
    // -----------------------------------------------------------------------
    void load(const std::string& filepath,
              size_t max_records = std::numeric_limits<size_t>::max())
    {
        CSVParser<Record311> parser(filepath);
        if (!parser.open()) {
            throw std::runtime_error("Cannot open: " + filepath);
        }
        Record311 rec;
        while (records_.size() < max_records && parser.readNext(rec)) {
            records_.push_back(rec);
        }
        parser.close();
    }

    // -----------------------------------------------------------------------
    // loadMultiple — reads several CSV files in parallel (one thread per
    // file) then merges results.  Useful when the 12 GB dataset is split
    // across yearly export files.
    // -----------------------------------------------------------------------
    void loadMultiple(const std::vector<std::string>& filepaths) {
        const int n = static_cast<int>(filepaths.size());
        std::vector<std::vector<Record311>> per_thread(static_cast<size_t>(n));

        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < n; ++i) {
            CSVParser<Record311> parser(filepaths[static_cast<size_t>(i)]);
            if (!parser.open()) continue;
            Record311 rec;
            while (parser.readNext(rec)) {
                per_thread[static_cast<size_t>(i)].push_back(rec);
            }
            parser.close();
        }

        size_t total = 0;
        for (auto& v : per_thread) total += v.size();
        records_.reserve(records_.size() + total);
        for (auto& v : per_thread) {
            records_.insert(records_.end(),
                            std::make_move_iterator(v.begin()),
                            std::make_move_iterator(v.end()));
        }
    }

    // -----------------------------------------------------------------------
    // IDataStore interface — all queries parallelised with OpenMP
    // -----------------------------------------------------------------------
    size_t size() const override { return records_.size(); }

    size_t memoryBytes() const override {
        return records_.capacity() * (sizeof(Record311) + 11 * 20);
    }

    std::vector<size_t> filterByBorough(const std::string& borough) const override {
        Borough target = boroughFromString(borough);
        std::vector<size_t> result;
        std::mutex mtx;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                if (records_[i].borough == target) local.push_back(i);
            }
            std::lock_guard<std::mutex> lk(mtx);
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByComplaintType(const std::string& type) const override {
        std::vector<size_t> result;
        std::mutex mtx;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                if (records_[i].complaint_type == type) local.push_back(i);
            }
            std::lock_guard<std::mutex> lk(mtx);
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByZipRange(uint32_t min_zip,
                                          uint32_t max_zip) const override {
        std::vector<size_t> result;
        std::mutex mtx;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                uint32_t z = records_[i].incident_zip;
                if (z >= min_zip && z <= max_zip) local.push_back(i);
            }
            std::lock_guard<std::mutex> lk(mtx);
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByGeoBox(double min_lat, double max_lat,
                                        double min_lon, double max_lon) const override {
        std::vector<size_t> result;
        std::mutex mtx;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                double la = records_[i].latitude;
                double lo = records_[i].longitude;
                if (la >= min_lat && la <= max_lat &&
                    lo >= min_lon && lo <= max_lon) {
                    local.push_back(i);
                }
            }
            std::lock_guard<std::mutex> lk(mtx);
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByDateRange(uint32_t start_ymd,
                                           uint32_t end_ymd) const override {
        std::vector<size_t> result;
        std::mutex mtx;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                uint32_t d = records_[i].created_ymd;
                if (d >= start_ymd && d <= end_ymd) local.push_back(i);
            }
            std::lock_guard<std::mutex> lk(mtx);
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::map<std::string, size_t> countByBorough() const override {
        // Thread-local accumulation then merge to avoid lock contention
        int nthreads = 1;
        #ifdef _OPENMP
        nthreads = omp_get_max_threads();
        #endif
        std::vector<std::map<std::string, size_t>> local(
            static_cast<size_t>(nthreads));

        #pragma omp parallel
        {
            int tid = 0;
            #ifdef _OPENMP
            tid = omp_get_thread_num();
            #endif
            auto& lmap = local[static_cast<size_t>(tid)];
            #pragma omp for schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                lmap[boroughToString(records_[i].borough)]++;
            }
        }

        std::map<std::string, size_t> result;
        for (auto& lmap : local) {
            for (auto& kv : lmap) result[kv.first] += kv.second;
        }
        return result;
    }

    std::map<std::string, size_t> countByComplaintType(size_t top_n = 10) const override {
        int nthreads = 1;
        #ifdef _OPENMP
        nthreads = omp_get_max_threads();
        #endif
        std::vector<std::map<std::string, size_t>> local(
            static_cast<size_t>(nthreads));

        #pragma omp parallel
        {
            int tid = 0;
            #ifdef _OPENMP
            tid = omp_get_thread_num();
            #endif
            auto& lmap = local[static_cast<size_t>(tid)];
            #pragma omp for schedule(static)
            for (size_t i = 0; i < records_.size(); ++i) {
                lmap[records_[i].complaint_type]++;
            }
        }

        std::map<std::string, size_t> freq;
        for (auto& lmap : local) {
            for (auto& kv : lmap) freq[kv.first] += kv.second;
        }

        std::vector<std::pair<std::string, size_t>> vec(freq.begin(), freq.end());
        std::partial_sort(vec.begin(),
                          vec.begin() + static_cast<std::ptrdiff_t>(
                              std::min(top_n, vec.size())),
                          vec.end(),
                          [](const auto& a, const auto& b){ return a.second > b.second; });

        std::map<std::string, size_t> result;
        for (size_t i = 0; i < std::min(top_n, vec.size()); ++i) {
            result[vec[i].first] = vec[i].second;
        }
        return result;
    }

    const Record311& at(size_t idx) const { return records_.at(idx); }

private:
    std::vector<Record311> records_;
};

}  // namespace nyc311
