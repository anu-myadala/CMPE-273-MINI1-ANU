#!/usr/bin/env python3
"""
generate_sample.py — creates a small synthetic NYC 311 CSV for smoke-testing
the C++ code before running on the full 12 GB dataset.

Usage:
    python3 data/generate_sample.py [rows] [output_file]

Defaults:
    rows        = 10000
    output_file = data/sample_311.csv

Column order matches the Socrata export:
    https://data.cityofnewyork.us/Social-Services/
    311-Service-Requests-from-2020-to-Present/erm2-nwe9
"""

import csv
import random
import sys
from datetime import datetime, timedelta

BOROUGHS = ["MANHATTAN", "BROOKLYN", "QUEENS", "BRONX", "STATEN ISLAND"]
AGENCIES = ["NYPD", "DEP", "DOT", "DSNY", "HPD", "DPR", "DOB", "FDNY"]
COMPLAINT_TYPES = [
    "Noise - Residential",
    "HEAT/HOT WATER",
    "Illegal Parking",
    "Blocked Driveway",
    "Street Light Condition",
    "PAINT/PLASTER",
    "Water System",
    "Noise - Commercial",
    "Rodent",
    "Unsanitary Condition",
    "Noise - Street/Sidewalk",
    "Traffic Signal Condition",
    "Dirty Conditions",
    "Graffiti",
    "Elevator",
]
DESCRIPTORS = [
    "Loud Music/Party",
    "No Heat",
    "Double Parked",
    "Partial Access",
    "Street Light Out",
    "Peeling",
    "No Running Water",
    "Banging/Pounding",
    "Rat Sighting",
    "Trash",
]
STATUSES = ["Open", "Closed", "Assigned", "Pending"]
CHANNELS = ["PHONE", "ONLINE", "MOBILE", "OTHER"]

# Borough bounding boxes [min_lat, max_lat, min_lon, max_lon]
BOROUGH_GEO = {
    "MANHATTAN":     (40.700, 40.882, -74.020, -73.907),
    "BROOKLYN":      (40.551, 40.739, -74.042, -73.833),
    "QUEENS":        (40.541, 40.800, -73.962, -73.700),
    "BRONX":         (40.785, 40.915, -73.933, -73.748),
    "STATEN ISLAND": (40.477, 40.651, -74.260, -74.034),
}
BOROUGH_ZIPS = {
    "MANHATTAN":     (10001, 10280),
    "BROOKLYN":      (11201, 11256),
    "QUEENS":        (11001, 11697),
    "BRONX":         (10451, 10475),
    "STATEN ISLAND": (10301, 10314),
}
BOROUGH_PRECINCTS = {
    "MANHATTAN":     (1, 34),
    "BROOKLYN":      (60, 94),
    "QUEENS":        (100, 115),
    "BRONX":         (40, 52),
    "STATEN ISLAND": (120, 123),
}

HEADER = [
    "unique_key", "created_date", "closed_date", "agency", "agency_name",
    "complaint_type", "descriptor", "descriptor_2", "location_type",
    "incident_zip", "incident_address", "street_name",
    "cross_street_1", "cross_street_2",
    "intersection_street_1", "intersection_street_2",
    "address_type", "city", "landmark", "facility_type",
    "status", "due_date", "resolution_description",
    "resolution_action_updated_date",
    "community_board", "council_district", "police_precinct",
    "bbl", "borough",
    "x_coordinate_state_plane", "y_coordinate_state_plane",
    "open_data_channel_type", "park_facility_name", "park_borough",
    "vehicle_type", "taxi_company_borough", "taxi_pick_up_location",
    "bridge_highway_name", "bridge_highway_direction",
    "road_ramp", "bridge_highway_segment",
    "latitude", "longitude", "location",
]


def rand_date(start: datetime, end: datetime) -> datetime:
    delta = end - start
    return start + timedelta(seconds=random.randint(0, int(delta.total_seconds())))


def iso_date(dt: datetime) -> str:
    return dt.strftime("%Y-%m-%dT%H:%M:%S.000")


def generate_row(uid: int) -> list:
    borough = random.choice(BOROUGHS)
    lat_min, lat_max, lon_min, lon_max = BOROUGH_GEO[borough]
    zip_min, zip_max = BOROUGH_ZIPS[borough]
    pre_min, pre_max = BOROUGH_PRECINCTS[borough]

    lat = round(random.uniform(lat_min, lat_max), 7)
    lon = round(random.uniform(lon_min, lon_max), 7)
    # Approximate NY State Plane (ft) from WGS-84 — rough conversion for sample data
    x = int((lon + 74.5) * 300000)
    y = int((lat - 40.0) * 365000)

    created = rand_date(datetime(2020, 1, 1), datetime(2024, 12, 31))
    is_closed = random.random() < 0.75
    closed = iso_date(created + timedelta(days=random.randint(1, 60))) if is_closed else ""

    agency = random.choice(AGENCIES)
    complaint = random.choice(COMPLAINT_TYPES)
    descriptor = random.choice(DESCRIPTORS)
    zipcode = random.randint(zip_min, zip_max)
    precinct = random.randint(pre_min, pre_max)
    council = random.randint(1, 51)
    board_num = random.randint(1, 18)

    return [
        uid,                                        # unique_key
        iso_date(created),                          # created_date
        closed,                                     # closed_date
        agency,                                     # agency
        agency + " Agency",                         # agency_name
        complaint,                                  # complaint_type
        descriptor,                                 # descriptor
        "",                                         # descriptor_2
        "RESIDENTIAL BUILDING",                     # location_type
        zipcode,                                    # incident_zip
        f"{random.randint(1,999)} MAIN ST",         # incident_address
        "MAIN ST",                                  # street_name
        "", "", "", "",                             # cross/intersection streets
        "ADDRESS",                                  # address_type
        borough.title(),                            # city
        "", "",                                     # landmark, facility_type
        "Closed" if is_closed else "Open",          # status
        "", "", "",                                 # due_date, resolution, action_date
        f"{board_num:02d} {borough}",               # community_board
        council,                                    # council_district
        precinct,                                   # police_precinct
        "",                                         # bbl
        borough,                                    # borough
        x,                                          # x_coordinate_state_plane
        y,                                          # y_coordinate_state_plane
        random.choice(CHANNELS),                    # open_data_channel_type
        "", "",                                     # park_facility_name, park_borough
        "", "", "",                                 # vehicle_type, taxi_*
        "", "", "", "",                             # bridge/highway fields
        lat,                                        # latitude
        lon,                                        # longitude
        f"({lat}, {lon})",                          # location
    ]


def main():
    rows = int(sys.argv[1]) if len(sys.argv) > 1 else 10_000
    out  = sys.argv[2] if len(sys.argv) > 2 else "data/sample_311.csv"

    with open(out, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, quoting=csv.QUOTE_MINIMAL)
        writer.writerow(HEADER)
        for i in range(rows):
            writer.writerow(generate_row(10_000_000 + i))

    print(f"Generated {rows:,} rows → {out}")


if __name__ == "__main__":
    main()
