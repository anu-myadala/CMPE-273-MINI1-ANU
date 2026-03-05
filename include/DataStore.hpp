#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "CSVParser.hpp"
#include "IDataStore.hpp"
#include "Record311.hpp"

namespace nyc311 {

// ---------------------------------------------------------------------------
// DataStore  —  Phase 1: serial Array-of-Structures container.
//
// Records are stored as std::vector<Record311> (AoS layout).  Every query
// is a single-threaded sequential scan of that vector.  This becomes the
// performance baseline for Phases 2 and 3.
// ---------------------------------------------------------------------------
class DataStore : public IDataStore {
public:
    // -----------------------------------------------------------------------
    // load — reads every record from one or more CSV files.
    // Call multiple times to append records from additional files.
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
    // IDataStore interface
    // -----------------------------------------------------------------------
    size_t size() const override { return records_.size(); }

    size_t memoryBytes() const override {
        // sizeof(Record311) × capacity  +  heap estimate for std::string fields
        // Each Record311 has ~11 std::string members; average string ≈ 20 bytes
        return records_.capacity() * (sizeof(Record311) + 11 * 20);
    }

    // Filter by exact borough name ("MANHATTAN", "BROOKLYN", etc.)
    std::vector<size_t> filterByBorough(const std::string& borough) const override {
        Borough target = boroughFromString(borough);
        std::vector<size_t> result;
        for (size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].borough == target) result.push_back(i);
        }
        return result;
    }

    // Filter by exact complaint type string
    std::vector<size_t> filterByComplaintType(const std::string& type) const override {
        std::vector<size_t> result;
        for (size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].complaint_type == type) result.push_back(i);
        }
        return result;
    }

    // Filter by zip-code range [min_zip, max_zip] inclusive
    std::vector<size_t> filterByZipRange(uint32_t min_zip,
                                          uint32_t max_zip) const override {
        std::vector<size_t> result;
        for (size_t i = 0; i < records_.size(); ++i) {
            uint32_t z = records_[i].incident_zip;
            if (z >= min_zip && z <= max_zip) result.push_back(i);
        }
        return result;
    }

    // Filter by geographic bounding box (WGS-84 decimal degrees)
    std::vector<size_t> filterByGeoBox(double min_lat, double max_lat,
                                        double min_lon, double max_lon) const override {
        std::vector<size_t> result;
        for (size_t i = 0; i < records_.size(); ++i) {
            double la = records_[i].latitude;
            double lo = records_[i].longitude;
            if (la >= min_lat && la <= max_lat &&
                lo >= min_lon && lo <= max_lon) {
                result.push_back(i);
            }
        }
        return result;
    }

    // Filter by YYYYMMDD date range on created_date
    std::vector<size_t> filterByDateRange(uint32_t start_ymd,
                                           uint32_t end_ymd) const override {
        std::vector<size_t> result;
        for (size_t i = 0; i < records_.size(); ++i) {
            uint32_t d = records_[i].created_ymd;
            if (d >= start_ymd && d <= end_ymd) result.push_back(i);
        }
        return result;
    }

    // Count records per borough
    std::map<std::string, size_t> countByBorough() const override {
        std::map<std::string, size_t> counts;
        for (const auto& r : records_) {
            counts[boroughToString(r.borough)]++;
        }
        return counts;
    }

    // Count records per complaint type, return top_n sorted by frequency
    std::map<std::string, size_t> countByComplaintType(size_t top_n = 10) const override {
        std::map<std::string, size_t> freq;
        for (const auto& r : records_) freq[r.complaint_type]++;

        // Sort descending by count and keep top_n
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

    // Direct record access for post-query materialisation
    const Record311& at(size_t idx) const { return records_.at(idx); }

private:
    std::vector<Record311> records_;
};

}  // namespace nyc311
