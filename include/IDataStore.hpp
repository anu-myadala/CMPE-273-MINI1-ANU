#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nyc311 {

// ---------------------------------------------------------------------------
// IDataStore — pure abstract interface for all three phases.
//
// Every phase implements identical query semantics so benchmarks are a fair
// apples-to-apples comparison.  Queries return index vectors; callers decide
// whether to materialise the actual records.
// ---------------------------------------------------------------------------
class IDataStore {
public:
    virtual ~IDataStore() = default;

    // Number of loaded records
    virtual size_t size() const = 0;

    // Approximate resident memory consumed by the store (bytes)
    virtual size_t memoryBytes() const = 0;

    // -----------------------------------------------------------------------
    // Range / filter queries — return indices of matching records
    // -----------------------------------------------------------------------

    // Filter by exact borough name (e.g. "BROOKLYN")
    virtual std::vector<size_t>
    filterByBorough(const std::string& borough) const = 0;

    // Filter by exact complaint type
    virtual std::vector<size_t>
    filterByComplaintType(const std::string& type) const = 0;

    // Zip-code range [min_zip, max_zip] inclusive
    virtual std::vector<size_t>
    filterByZipRange(uint32_t min_zip, uint32_t max_zip) const = 0;

    // Geographic bounding box (WGS-84 decimal degrees)
    virtual std::vector<size_t>
    filterByGeoBox(double min_lat, double max_lat,
                   double min_lon, double max_lon) const = 0;

    // YYYYMMDD date range on created_date
    virtual std::vector<size_t>
    filterByDateRange(uint32_t start_ymd, uint32_t end_ymd) const = 0;

    // -----------------------------------------------------------------------
    // Aggregate queries
    // -----------------------------------------------------------------------

    // Record count keyed by borough
    virtual std::map<std::string, size_t> countByBorough() const = 0;

    // Top-N complaint types by frequency
    virtual std::map<std::string, size_t>
    countByComplaintType(size_t top_n = 10) const = 0;
};

}  // namespace nyc311
