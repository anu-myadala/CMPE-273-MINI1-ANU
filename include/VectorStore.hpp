#pragma once
#include <algorithm>
#include <cstddef>
#include <map>
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
// VectorStore  —  Phase 3: Structure-of-Arrays (SoA) vectorized container.
//
// Unlike AoS (DataStore / ParallelDataStore) where each record is one object
// in a contiguous array, here every field has its own vector.  When a query
// only touches one or two fields (e.g. filterByGeoBox only needs latitude[]
// and longitude[]) the CPU cache sees only the relevant data — no wasted
// cache-line bandwidth from unrelated fields interleaved in each struct.
//
// This layout enables:
//   (a) Auto-vectorisation (SIMD) of numeric-field loops by the compiler.
//   (b) Minimal cache pressure for selective-column queries.
//   (c) Easy OpenMP parallelism with no false-sharing on hot arrays.
//
// The trade-off: full-record access requires touching N separate vectors,
// which is less convenient and slightly slower for operations that need all
// fields at once.
// ---------------------------------------------------------------------------
class VectorStore : public IDataStore {
public:
    // -----------------------------------------------------------------------
    // load — reads one CSV file into the SoA vectors.
    // -----------------------------------------------------------------------
    void load(const std::string& filepath,
              size_t max_records = std::numeric_limits<size_t>::max())
    {
        CSVParser<Record311> parser(filepath);
        if (!parser.open()) {
            throw std::runtime_error("Cannot open: " + filepath);
        }

        Record311 rec;
        while (size() < max_records && parser.readNext(rec)) {
            appendRecord(rec);
        }
        parser.close();
    }

    void loadMultiple(const std::vector<std::string>& filepaths) {
        const int n = static_cast<int>(filepaths.size());
        std::vector<std::vector<Record311>> per_file(static_cast<size_t>(n));

        #pragma omp parallel for schedule(dynamic, 1)
        for (int i = 0; i < n; ++i) {
            CSVParser<Record311> parser(filepaths[static_cast<size_t>(i)]);
            if (!parser.open()) continue;
            Record311 rec;
            while (parser.readNext(rec)) {
                per_file[static_cast<size_t>(i)].push_back(rec);
            }
            parser.close();
        }

        size_t total = 0;
        for (auto& v : per_file) total += v.size();
        reserveExtra(total);

        for (auto& v : per_file) {
            for (auto& rec : v) appendRecord(rec);
        }
    }

    // -----------------------------------------------------------------------
    // IDataStore interface
    // -----------------------------------------------------------------------
    size_t size() const override { return unique_keys.size(); }

    size_t memoryBytes() const override {
        // Numeric vectors: exact
        size_t bytes = unique_keys.capacity()   * sizeof(uint64_t)
                     + created_ymds.capacity()  * sizeof(uint32_t)
                     + closed_ymds.capacity()   * sizeof(uint32_t)
                     + incident_zips.capacity() * sizeof(uint32_t)
                     + boroughs.capacity()       * sizeof(Borough)
                     + community_boards.capacity() * sizeof(uint16_t)
                     + council_districts.capacity() * sizeof(uint16_t)
                     + police_precincts.capacity()  * sizeof(uint16_t)
                     + latitudes.capacity()     * sizeof(double)
                     + longitudes.capacity()    * sizeof(double)
                     + x_coords.capacity()      * sizeof(int32_t)
                     + y_coords.capacity()      * sizeof(int32_t);
        // String vectors: estimate
        bytes += (created_dates.capacity() + closed_dates.capacity()
                + agencies.capacity() + complaint_types.capacity()
                + descriptors.capacity() + descriptor_2s.capacity()
                + cities.capacity() + statuses.capacity()
                + channel_types.capacity()) * 20;
        return bytes;
    }

    // -----------------------------------------------------------------------
    // Queries — each one scans only the field vectors it needs.
    // Numeric loops are written to allow the compiler to auto-vectorise.
    // -----------------------------------------------------------------------

    std::vector<size_t> filterByBorough(const std::string& borough) const override {
        Borough target = boroughFromString(borough);
        const size_t n  = boroughs.size();
        std::vector<size_t> result;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < n; ++i) {
                if (boroughs[i] == target) local.push_back(i);
            }
            #pragma omp critical
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByComplaintType(const std::string& type) const override {
        const size_t n = complaint_types.size();
        std::vector<size_t> result;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < n; ++i) {
                if (complaint_types[i] == type) local.push_back(i);
            }
            #pragma omp critical
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    // zip scan — only the incident_zips vector is touched
    std::vector<size_t> filterByZipRange(uint32_t min_zip,
                                          uint32_t max_zip) const override {
        const size_t n = incident_zips.size();
        std::vector<size_t> result;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < n; ++i) {
                uint32_t z = incident_zips[i];
                if (z >= min_zip && z <= max_zip) local.push_back(i);
            }
            #pragma omp critical
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    // geo box — only latitudes[] and longitudes[] are touched; this is where
    // SoA cache efficiency is most visible vs AoS.
    std::vector<size_t> filterByGeoBox(double min_lat, double max_lat,
                                        double min_lon, double max_lon) const override {
        const size_t n = latitudes.size();
        std::vector<size_t> result;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < n; ++i) {
                double la = latitudes[i];
                double lo = longitudes[i];
                if (la >= min_lat && la <= max_lat &&
                    lo >= min_lon && lo <= max_lon) {
                    local.push_back(i);
                }
            }
            #pragma omp critical
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::vector<size_t> filterByDateRange(uint32_t start_ymd,
                                           uint32_t end_ymd) const override {
        const size_t n = created_ymds.size();
        std::vector<size_t> result;

        #pragma omp parallel
        {
            std::vector<size_t> local;
            #pragma omp for nowait schedule(static)
            for (size_t i = 0; i < n; ++i) {
                uint32_t d = created_ymds[i];
                if (d >= start_ymd && d <= end_ymd) local.push_back(i);
            }
            #pragma omp critical
            result.insert(result.end(), local.begin(), local.end());
        }
        return result;
    }

    std::map<std::string, size_t> countByBorough() const override {
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
            for (size_t i = 0; i < boroughs.size(); ++i) {
                lmap[boroughToString(boroughs[i])]++;
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
            for (size_t i = 0; i < complaint_types.size(); ++i) {
                lmap[complaint_types[i]]++;
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

    // -----------------------------------------------------------------------
    // Public SoA columns — direct access for advanced callers
    // -----------------------------------------------------------------------
    std::vector<uint64_t>    unique_keys;
    std::vector<std::string> created_dates;
    std::vector<uint32_t>    created_ymds;
    std::vector<std::string> closed_dates;
    std::vector<uint32_t>    closed_ymds;
    std::vector<std::string> agencies;
    std::vector<std::string> complaint_types;
    std::vector<std::string> descriptors;
    std::vector<std::string> descriptor_2s;
    std::vector<uint32_t>    incident_zips;
    std::vector<std::string> cities;
    std::vector<Borough>     boroughs;
    std::vector<uint16_t>    community_boards;
    std::vector<uint16_t>    council_districts;
    std::vector<uint16_t>    police_precincts;
    std::vector<std::string> statuses;
    std::vector<double>      latitudes;
    std::vector<double>      longitudes;
    std::vector<int32_t>     x_coords;
    std::vector<int32_t>     y_coords;
    std::vector<std::string> channel_types;

private:
    void reserveExtra(size_t n) {
        auto grow = [&](auto& v){ v.reserve(v.size() + n); };
        grow(unique_keys);   grow(created_dates); grow(created_ymds);
        grow(closed_dates);  grow(closed_ymds);   grow(agencies);
        grow(complaint_types); grow(descriptors); grow(descriptor_2s);
        grow(incident_zips); grow(cities);        grow(boroughs);
        grow(community_boards); grow(council_districts);
        grow(police_precincts); grow(statuses);
        grow(latitudes);     grow(longitudes);
        grow(x_coords);      grow(y_coords);      grow(channel_types);
    }

    void appendRecord(const Record311& r) {
        unique_keys.push_back(r.unique_key);
        created_dates.push_back(r.created_date);
        created_ymds.push_back(r.created_ymd);
        closed_dates.push_back(r.closed_date);
        closed_ymds.push_back(r.closed_ymd);
        agencies.push_back(r.agency);
        complaint_types.push_back(r.complaint_type);
        descriptors.push_back(r.descriptor);
        descriptor_2s.push_back(r.descriptor_2);
        incident_zips.push_back(r.incident_zip);
        cities.push_back(r.city);
        boroughs.push_back(r.borough);
        community_boards.push_back(r.community_board);
        council_districts.push_back(r.council_district);
        police_precincts.push_back(r.police_precinct);
        statuses.push_back(r.status);
        latitudes.push_back(r.latitude);
        longitudes.push_back(r.longitude);
        x_coords.push_back(r.x_coord);
        y_coords.push_back(r.y_coord);
        channel_types.push_back(r.open_data_channel_type);
    }
};

}  // namespace nyc311
