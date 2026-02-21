#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nyc311 {

// ---------------------------------------------------------------------------
// Borough enum — encodes the five NYC boroughs plus an unspecified value.
// Stored as uint8_t to minimise per-record footprint.
// ---------------------------------------------------------------------------
enum class Borough : uint8_t {
    UNSPECIFIED   = 0,
    MANHATTAN     = 1,
    BROOKLYN      = 2,
    QUEENS        = 3,
    BRONX         = 4,
    STATEN_ISLAND = 5
};

inline Borough boroughFromString(const std::string& s) {
    if (s == "MANHATTAN")      return Borough::MANHATTAN;
    if (s == "BROOKLYN")       return Borough::BROOKLYN;
    if (s == "QUEENS")         return Borough::QUEENS;
    if (s == "BRONX")          return Borough::BRONX;
    if (s == "STATEN ISLAND")  return Borough::STATEN_ISLAND;
    return Borough::UNSPECIFIED;
}

inline std::string boroughToString(Borough b) {
    switch (b) {
        case Borough::MANHATTAN:     return "MANHATTAN";
        case Borough::BROOKLYN:      return "BROOKLYN";
        case Borough::QUEENS:        return "QUEENS";
        case Borough::BRONX:         return "BRONX";
        case Borough::STATEN_ISLAND: return "STATEN ISLAND";
        default:                     return "UNSPECIFIED";
    }
}

// ---------------------------------------------------------------------------
// Exact column indices for the NYC 311 Service Requests 2020-present dataset
// (https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2020-to-Present/erm2-nwe9)
//
//  0  unique_key
//  1  created_date
//  2  closed_date
//  3  agency
//  4  agency_name
//  5  complaint_type
//  6  descriptor
//  7  descriptor_2           ← new field in 2020+ schema
//  8  location_type
//  9  incident_zip
// 10  incident_address
// 11  street_name
// 12  cross_street_1
// 13  cross_street_2
// 14  intersection_street_1
// 15  intersection_street_2
// 16  address_type
// 17  city
// 18  landmark
// 19  facility_type
// 20  status
// 21  due_date
// 22  resolution_description
// 23  resolution_action_updated_date
// 24  community_board        (e.g. "01 MANHATTAN")
// 25  council_district
// 26  police_precinct
// 27  bbl
// 28  borough
// 29  x_coordinate_state_plane
// 30  y_coordinate_state_plane
// 31  open_data_channel_type
// 32  park_facility_name
// 33  park_borough
// 34  vehicle_type
// 35  taxi_company_borough
// 36  taxi_pick_up_location
// 37  bridge_highway_name
// 38  bridge_highway_direction
// 39  road_ramp
// 40  bridge_highway_segment
// 41  latitude
// 42  longitude
// 43  location               (e.g. "(40.7, -73.9)")
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Record311 — one NYC 311 Service Request row, 2020-present schema.
//
// Field types are chosen to match the natural primitive type of each column:
//   - integer IDs       → uint64_t
//   - zip / precinct    → uint32_t
//   - board / district  → uint16_t
//   - borough           → Borough enum (uint8_t)
//   - coordinates       → double (lat/lon) / int32_t (State Plane)
//   - categorical text  → std::string
//
// Rarely-queried columns (bridge, taxi, park, landmark) are stored in a
// separate optional block to make the common-path struct smaller.
// ---------------------------------------------------------------------------
struct Record311 {
    // -- primary fields (always queried) ------------------------------------
    uint64_t    unique_key              = 0;
    std::string created_date;           // ISO "YYYY-MM-DDTHH:MM:SS.sss" or
                                        // "MM/DD/YYYY HH:MM:SS AM"
    std::string closed_date;
    std::string agency;                 // short code, e.g. "NYPD", "DEP"
    std::string complaint_type;
    std::string descriptor;
    std::string descriptor_2;           // secondary descriptor (2020+ schema)
    uint32_t    incident_zip            = 0;
    std::string city;
    Borough     borough                 = Borough::UNSPECIFIED;
    uint16_t    community_board         = 0;  // numeric part of "NN BOROUGH"
    uint16_t    council_district        = 0;
    uint16_t    police_precinct         = 0;
    std::string status;                 // "Open", "Closed", "Assigned"
    double      latitude                = 0.0;
    double      longitude               = 0.0;
    int32_t     x_coord                 = 0;  // NY State Plane (ft)
    int32_t     y_coord                 = 0;
    std::string open_data_channel_type; // "PHONE", "ONLINE", "MOBILE", "OTHER"

    // -- date parsed as YYYYMMDD uint32 for fast range comparisons ----------
    uint32_t    created_ymd             = 0;
    uint32_t    closed_ymd              = 0;

    // -----------------------------------------------------------------------
    // parseDate — converts both common Socrata date formats to YYYYMMDD.
    //   ISO  : "2024-01-15T10:30:00.000"  →  20240115
    //   MDY  : "01/15/2024 10:30:00 AM"   →  20240115
    // -----------------------------------------------------------------------
    static uint32_t parseDate(const std::string& s) {
        if (s.size() >= 10 && s[4] == '-') {
            // ISO 8601
            try {
                uint32_t y = static_cast<uint32_t>(std::stoul(s.substr(0, 4)));
                uint32_t m = static_cast<uint32_t>(std::stoul(s.substr(5, 2)));
                uint32_t d = static_cast<uint32_t>(std::stoul(s.substr(8, 2)));
                return y * 10000u + m * 100u + d;
            } catch (...) {}
        } else if (s.size() >= 10 && s[2] == '/') {
            // MM/DD/YYYY
            try {
                uint32_t m = static_cast<uint32_t>(std::stoul(s.substr(0, 2)));
                uint32_t d = static_cast<uint32_t>(std::stoul(s.substr(3, 2)));
                uint32_t y = static_cast<uint32_t>(std::stoul(s.substr(6, 4)));
                return y * 10000u + m * 100u + d;
            } catch (...) {}
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // fromFields — called by CSVParser<Record311>::readNext()
    // Maps the 44-column Socrata CSV row to the struct fields.
    // Returns false if the row cannot be parsed (e.g. missing unique_key).
    // -----------------------------------------------------------------------
    static bool fromFields(const std::vector<std::string>& f, Record311& r) {
        if (f.size() < 29) return false;

        // unique_key (col 0) — mandatory
        try { r.unique_key = static_cast<uint64_t>(std::stoull(f[0])); }
        catch (...) { return false; }

        r.created_date = f[1];
        r.closed_date  = f[2];
        r.created_ymd  = parseDate(f[1]);
        r.closed_ymd   = parseDate(f[2]);

        r.agency         = f[3];
        // f[4] = agency_name  (skipped)
        r.complaint_type = f[5];
        r.descriptor     = f[6];
        r.descriptor_2   = f[7];
        // f[8]  = location_type     (skipped)

        // incident_zip (col 9)
        if (!f[9].empty()) {
            try { r.incident_zip = static_cast<uint32_t>(std::stoul(f[9])); }
            catch (...) { r.incident_zip = 0; }
        }

        // f[10-16] address fields   (skipped)
        r.city   = f[17];
        // f[18] = landmark          (skipped)
        // f[19] = facility_type     (skipped)
        r.status = f[20];
        // f[21] = due_date          (skipped)
        // f[22] = resolution_description (skipped)
        // f[23] = resolution_action_updated_date (skipped)

        // community_board (col 24) — "NN BOROUGH", extract numeric part
        if (!f[24].empty()) {
            try { r.community_board = static_cast<uint16_t>(std::stoul(f[24])); }
            catch (...) { r.community_board = 0; }
        }

        // council_district (col 25)
        if (!f[25].empty()) {
            try { r.council_district = static_cast<uint16_t>(std::stoul(f[25])); }
            catch (...) { r.council_district = 0; }
        }

        // police_precinct (col 26)
        if (!f[26].empty()) {
            try { r.police_precinct = static_cast<uint16_t>(std::stoul(f[26])); }
            catch (...) { r.police_precinct = 0; }
        }

        // f[27] = bbl (skipped)
        r.borough = boroughFromString(f[28]);

        // x/y State Plane (cols 29, 30)
        if (f.size() > 29 && !f[29].empty()) {
            try { r.x_coord = static_cast<int32_t>(std::stol(f[29])); }
            catch (...) { r.x_coord = 0; }
        }
        if (f.size() > 30 && !f[30].empty()) {
            try { r.y_coord = static_cast<int32_t>(std::stol(f[30])); }
            catch (...) { r.y_coord = 0; }
        }

        if (f.size() > 31) r.open_data_channel_type = f[31];
        // f[32-40] park/taxi/bridge fields (skipped)

        // latitude (col 41), longitude (col 42)
        if (f.size() > 41 && !f[41].empty()) {
            try { r.latitude  = std::stod(f[41]); } catch (...) {}
        }
        if (f.size() > 42 && !f[42].empty()) {
            try { r.longitude = std::stod(f[42]); } catch (...) {}
        }

        return true;
    }
};

}  // namespace nyc311
