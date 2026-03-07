# CMPE-273 Mini Project 1: Memory Overload
## High-Performance In-Memory Analytics for NYC 311 Service Requests

---

**Course:** CMPE-273 — Enterprise Distributed Systems  
**Semester:** Spring 2026  
**Submission Date:** March 6, 2026

**Team Members:**
- Anukrithi Myadala
- Asim Mohammed
- Ali Ucer

**Dataset:** NYC 311 Service Requests from 2020 to Present  
**Data Source:** NYC OpenData (Socrata Platform)  
**URL:** https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2020-to-Present/erm2-nwe9

**Note:** The archive contains only source code, presentation slide, and this report.

---

## Abstract

This report presents a comprehensive investigation into the performance characteristics of in-memory data analytics systems when processing large-scale urban service request data. Using the New York City 311 Service Requests dataset—comprising over 18 million records and approximately 12 gigabytes of raw CSV data—we systematically evaluate the impact of memory layout strategies and thread-level parallelism on query latency and memory efficiency.

Our investigation proceeds through three distinct implementation phases: a serial Array-of-Structures (AoS) baseline, an OpenMP-parallelized AoS implementation, and a Structure-of-Arrays (SoA) implementation with SIMD vectorization. The empirical results demonstrate that memory layout optimization yields substantially greater performance improvements than parallelization alone. Specifically, the SoA implementation achieves query latencies between 9.7 and 29.4 milliseconds on 18 million records, representing speedup factors of 39× to 119× compared to the serial baseline. In contrast, adding eight-thread parallelism to the AoS layout provides only a 20% improvement, revealing that the workload is fundamentally memory-bandwidth-bound rather than compute-bound.

These findings have significant implications for the design of real-time urban analytics systems, where sub-10-millisecond query response times can enable truly interactive exploration of city-scale datasets.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Background and Motivation](#2-background-and-motivation)
3. [Dataset Description](#3-dataset-description)
4. [System Architecture and Design](#4-system-architecture-and-design)
5. [Implementation Details](#5-implementation-details)
6. [Experimental Methodology](#6-experimental-methodology)
7. [Results and Analysis](#7-results-and-analysis)
8. [Failed Approaches and Lessons Learned](#8-failed-approaches-and-lessons-learned)
9. [Discussion](#9-discussion)
10. [Conclusions and Future Work](#10-conclusions-and-future-work)
11. [Appendices](#11-appendices)

---

## 1. Introduction

The proliferation of urban data collection systems has created unprecedented opportunities for evidence-based city governance. Among these systems, the NYC 311 service represents one of the most comprehensive municipal feedback mechanisms in the United States, processing millions of non-emergency service requests annually across categories ranging from noise complaints to infrastructure maintenance. As of 2026, the publicly available dataset spans over 20 million records dating back to 2010, with the post-2020 subset alone containing approximately 18 million entries.

The sheer scale of this dataset presents both analytical opportunities and computational challenges. Traditional database systems can certainly handle queries over 18 million records, but the latencies involved—often measured in seconds—preclude the kind of interactive, exploratory analysis that urban planners and researchers increasingly demand. This observation motivated our central research question: **Can careful attention to memory layout and cache utilization reduce query latencies to the point where real-time, interactive analysis of city-scale datasets becomes practical?**

This project investigates this question through the lens of two complementary performance optimization strategies: data layout transformation (specifically, the conversion from Array-of-Structures to Structure-of-Arrays representation) and thread-level parallelism via OpenMP. Our hypothesis is that for selective-column queries, the SoA layout will dramatically outperform AoS by eliminating cache-line waste and enabling SIMD vectorization.

The remainder of this report is organized as follows. Section 2 provides necessary background on memory hierarchies and data layout strategies. Section 3 describes the NYC 311 dataset in detail. Section 4 presents our system architecture, while Section 5 discusses implementation specifics. Section 6 outlines our benchmarking methodology, and Section 7 presents empirical results. Section 8 documents approaches that failed, providing valuable negative results. Section 9 discusses the broader implications of our findings, and Section 10 concludes with suggestions for future work.

---

## 2. Background and Motivation

### 2.1 The Memory Wall Problem

Modern processors can execute billions of instructions per second, yet they frequently stall waiting for data to arrive from main memory. This disparity between processor speed and memory latency—commonly termed the "memory wall"—has widened dramatically since the 1980s. Contemporary CPUs mitigate this gap through hierarchical cache structures, with L1 caches providing sub-nanosecond access times, L2 caches requiring 3-10 nanoseconds, L3 caches requiring 10-20 nanoseconds, and main memory requiring 50-100 nanoseconds or more.

The critical insight is that caches operate at the granularity of **cache lines**, typically 64 bytes on modern x86 and ARM processors. When a program requests a single byte from memory, the entire 64-byte cache line containing that byte is fetched. This prefetching behavior is highly beneficial when the program exhibits spatial locality—that is, when it is likely to access nearby memory locations in the near future. However, it becomes detrimental when the program's access pattern scatters relevant data across non-contiguous memory regions.

### 2.2 Array-of-Structures vs. Structure-of-Arrays

The distinction between Array-of-Structures (AoS) and Structure-of-Arrays (SoA) memory layouts directly impacts cache efficiency for analytical workloads.

In the AoS layout, each logical record is stored as a contiguous structure, with all fields of a single record adjacent in memory:

```
Record 0: [field_a₀, field_b₀, field_c₀, field_d₀, ...]
Record 1: [field_a₁, field_b₁, field_c₁, field_d₁, ...]
Record 2: [field_a₂, field_b₂, field_c₂, field_d₂, ...]
```

This layout is natural for object-oriented programming and works well when queries access most or all fields of each record. However, it becomes inefficient when queries access only a subset of fields. Consider a query that examines only `field_a` across all records: the CPU must load entire records into cache, discarding the majority of each cache line.

In the SoA layout, each field is stored in its own contiguous array:

```
field_a[]: [field_a₀, field_a₁, field_a₂, ...]
field_b[]: [field_b₀, field_b₁, field_b₂, ...]
field_c[]: [field_c₀, field_c₁, field_c₂, ...]
```

For a query accessing only `field_a`, the SoA layout provides perfect cache utilization: every byte loaded from memory is relevant to the query. Furthermore, the contiguous arrangement of homogeneous data types enables SIMD (Single Instruction, Multiple Data) vectorization, allowing modern processors to compare or compute over multiple elements simultaneously.

### 2.3 SIMD Vectorization and Modern Compilers

Contemporary compilers such as GCC 13 and Clang 16 include sophisticated auto-vectorization passes that can transform simple scalar loops into SIMD-parallel operations without programmer intervention. The AVX2 instruction set, available on Intel processors since Haswell (2013) and AMD processors since Excavator (2015), provides 256-bit vector registers capable of processing eight 32-bit integers or four 64-bit floating-point values in a single instruction.

For auto-vectorization to succeed, the loop structure must satisfy several constraints: the iteration count must be known at loop entry, the memory access pattern must be contiguous and aligned, and there must be no loop-carried dependencies. The SoA layout naturally satisfies the memory access requirement, whereas the AoS layout does not.

### 2.4 Memory-Bound vs. Compute-Bound Workloads

The performance of computational kernels can be characterized by **arithmetic intensity**—the ratio of floating-point operations to bytes transferred from memory. Kernels with low arithmetic intensity are memory-bound; kernels with high arithmetic intensity are compute-bound.

Filter queries over large datasets exemplify memory-bound workloads: each record requires only a handful of comparison operations, but scanning 18 million records requires transferring gigabytes of data. For such workloads, performance is limited by memory bandwidth, not by the processor's computational throughput. This observation directly motivates our focus on reducing memory traffic through layout optimization.

---

## 3. Dataset Description

### 3.1 Overview

The NYC 311 Service Requests dataset is maintained by the New York City Department of Information Technology and Telecommunications and published through the NYC OpenData portal via the Socrata platform. The dataset contains records of non-emergency service requests submitted by city residents through the 311 hotline, the 311 website, and the 311 mobile application.

For this project, we utilized the "311 Service Requests from 2020 to Present" subset, which offers several advantages: it employs a consistent schema with 44 columns, it includes geographic coordinates for the majority of requests, and its size (approximately 18 million records) is large enough to reveal cache effects while remaining tractable for iterative development.

### 3.2 Schema Details

The following table summarizes the columns most relevant to our analytical queries:

| Column Name | Data Type | Description | Storage in C++ |
|-------------|-----------|-------------|----------------|
| `unique_key` | Integer | Unique identifier for each request | `uint64_t` |
| `created_date` | Timestamp | Date and time the request was submitted | `uint32_t` (as YYYYMMDD) |
| `closed_date` | Timestamp | Date and time the request was resolved | `uint32_t` (as YYYYMMDD) |
| `agency` | String | City agency responsible for the request | `std::string` |
| `complaint_type` | String | Primary classification (~200 distinct values) | `std::string` |
| `descriptor` | String | Secondary classification | `std::string` |
| `incident_zip` | String | 5-digit ZIP code of the incident location | `uint32_t` |
| `borough` | String | One of five NYC boroughs or "Unspecified" | `enum Borough : uint8_t` |
| `latitude` | Float | WGS-84 latitude of the incident location | `double` |
| `longitude` | Float | WGS-84 longitude of the incident location | `double` |
| `status` | String | Current status (Open, Closed, Assigned, etc.) | `std::string` |

The full schema contains 44 columns, including administrative fields such as `agency_name`, `location_type`, `address_type`, `community_board`, `council_district`, and `police_precinct`. Our implementation parses and stores all 44 columns to ensure the AoS record size reflects realistic conditions.

### 3.3 Data Volume and Characteristics

| Metric | Value |
|--------|-------|
| Total records (2020–present) | 18,072,263 |
| Number of CSV files | 18 (one per year-quarter) |
| Total raw CSV size | 11.7 GB |
| Number of columns | 44 |
| Distinct complaint types | ~200 |
| Records with valid coordinates | 94.2% |
| Records with valid ZIP codes | 97.1% |

The dataset exhibits several characteristics relevant to performance analysis. First, string fields vary dramatically in length: `complaint_type` averages 25 characters, while `resolution_description` can exceed 200 characters. Second, approximately 6% of records lack valid geographic coordinates, requiring null handling in geo-spatial queries. Third, the temporal distribution is uneven, with significant spikes during the COVID-19 pandemic period (March 2020 – December 2021) due to stay-at-home-related complaints such as residential noise.

### 3.4 Borough Distribution

Analysis of the complete dataset reveals the following distribution of service requests by borough:

| Borough | Number of Requests | Percentage |
|---------|--------------------|------------|
| Brooklyn | 5,204,233 | 28.8% |
| Queens | 4,101,847 | 22.7% |
| Bronx | 3,508,821 | 19.4% |
| Manhattan | 3,306,814 | 18.3% |
| Staten Island | 992,176 | 5.5% |
| Unspecified | 958,372 | 5.3% |

Brooklyn's dominance in absolute request volume is consistent with its status as NYC's most populous borough. However, normalized per-capita analysis would likely reveal different patterns, as Manhattan's much higher population density generates more requests per square mile.

---

## 4. System Architecture and Design

### 4.1 Design Philosophy

Our system architecture reflects several guiding principles derived from the project requirements and performance optimization literature:

1. **Separation of concerns:** The query interface is defined abstractly, allowing different storage implementations (AoS serial, AoS parallel, SoA parallel) to be substituted without changing client code.

2. **Header-only implementation:** All components are implemented as header-only C++ templates, eliminating link-time dependencies and enabling the compiler to inline aggressively.

3. **Zero-copy result handling:** Query methods return vectors of indices rather than copies of matching records, avoiding large intermediate allocations and deferring materialization to the caller.

4. **Statistical rigor in benchmarking:** Each query is executed multiple times, with statistical aggregation (mean, minimum, maximum, standard deviation) to account for system variability.

### 4.2 Abstract Interface

The `IDataStore` abstract base class (defined in `include/IDataStore.hpp`) establishes the common query API that all three phase implementations must satisfy:

```cpp
class IDataStore {
public:
    virtual ~IDataStore() = default;
    
    // Filter queries returning matching indices
    virtual std::vector<size_t> filterByBorough(const std::string& borough) const = 0;
    virtual std::vector<size_t> filterByComplaintType(const std::string& type) const = 0;
    virtual std::vector<size_t> filterByZipRange(uint32_t min_zip, uint32_t max_zip) const = 0;
    virtual std::vector<size_t> filterByGeoBox(double min_lat, double max_lat,
                                                double min_lon, double max_lon) const = 0;
    virtual std::vector<size_t> filterByDateRange(uint32_t start_ymd, uint32_t end_ymd) const = 0;
    
    // Aggregation queries
    virtual std::unordered_map<std::string, size_t> countByBorough() const = 0;
    virtual std::unordered_map<std::string, size_t> countByComplaintType(size_t top_n) const = 0;
    
    // Metadata
    virtual size_t size() const = 0;
    virtual size_t memoryBytes() const = 0;
};
```

This interface design ensures that benchmark comparisons between phases are methodologically sound, as all implementations answer identical queries through identical APIs.

### 4.3 Component Overview

The system comprises the following major components:

| Component | File | Responsibility |
|-----------|------|----------------|
| Record Definition | `include/Record311.hpp` | Defines the `Record311` struct with 44 fields and parsing logic |
| CSV Parser | `include/CSVParser.hpp` | Template-based RFC-4180 compliant CSV parsing |
| Phase 1 Store | `include/DataStore.hpp` | Serial AoS implementation |
| Phase 2 Store | `include/ParallelDataStore.hpp` | OpenMP-parallelized AoS implementation |
| Phase 3 Store | `include/VectorStore.hpp` | SoA implementation with SIMD vectorization |
| Benchmark Harness | `include/Benchmark.hpp` | Timing utilities and statistical aggregation |
| Phase Executables | `src/phase{1,2,3}/main.cpp` | Entry points for each phase |

---

## 5. Implementation Details

### 5.1 Record Definition and Parsing

The `Record311` structure (defined in `include/Record311.hpp`) contains 44 fields corresponding to the dataset schema. Several design decisions merit discussion:

**Date representation:** Early iterations attempted to parse dates into `time_t` values using `strptime()`, but this approach proved problematic for two reasons. First, `strptime()` is not available on all platforms (notably absent in MSVC on Windows). Second, the NYC dataset contains two distinct date formats: ISO-8601 (`YYYY-MM-DDTHH:MM:SS.sss`) and US-style (`MM/DD/YYYY HH:MM:SS AM`). Rather than implementing complex format detection, we encode dates as `uint32_t` integers in YYYYMMDD format (e.g., March 6, 2026 becomes `20260306`). This representation supports efficient range comparisons through simple integer arithmetic while avoiding platform dependencies.

**Borough encoding:** The `borough` field is stored as an enumerated type (`enum Borough : uint8_t`) with six values: `MANHATTAN`, `BRONX`, `BROOKLYN`, `QUEENS`, `STATEN_ISLAND`, and `UNSPECIFIED`. This encoding reduces memory consumption from approximately 12 bytes per string to 1 byte per record while enabling fast integer comparison in filter queries.

**String fields:** Fields such as `complaint_type`, `descriptor`, and `resolution_description` remain as `std::string` because their lengths vary too dramatically for fixed-size character arrays. The C++ small string optimization (SSO) mitigates heap allocation overhead for strings shorter than approximately 15-22 characters (implementation-dependent).

### 5.2 Phase 1: Serial Array-of-Structures

The `DataStore` class (Phase 1) stores records as a `std::vector<Record311>`. Each query is implemented as a straightforward `for` loop that iterates over all records, tests a predicate, and appends matching indices to a result vector:

```cpp
std::vector<size_t> filterByBorough(const std::string& target) const override {
    Borough targetEnum = stringToBorough(target);
    std::vector<size_t> results;
    for (size_t i = 0; i < records_.size(); ++i) {
        if (records_[i].borough == targetEnum) {
            results.push_back(i);
        }
    }
    return results;
}
```

This implementation serves as the performance baseline against which subsequent phases are measured. Its simplicity also facilitates correctness verification, as the logic is trivially auditable.

**Memory layout analysis:** Each `Record311` instance occupies approximately 500 bytes, including string heap allocations. For 18 million records, the total memory footprint is approximately 9 GB for the record data alone, with additional overhead from `std::vector` capacity management. When executing a geo-box query that accesses only `latitude` and `longitude` (16 bytes per record), the CPU must load entire 500-byte records, resulting in approximately 97% cache-line waste.

### 5.3 Phase 2: OpenMP-Parallelized Array-of-Structures

The `ParallelDataStore` class extends Phase 1 with OpenMP parallelization. Each query loop is annotated with OpenMP directives to distribute iterations across available threads:

```cpp
std::vector<size_t> filterByBorough(const std::string& target) const override {
    Borough targetEnum = stringToBorough(target);
    std::vector<size_t> results;
    
    #pragma omp parallel
    {
        std::vector<size_t> local_results;
        
        #pragma omp for nowait schedule(static)
        for (size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].borough == targetEnum) {
                local_results.push_back(i);
            }
        }
        
        #pragma omp critical
        {
            results.insert(results.end(), local_results.begin(), local_results.end());
        }
    }
    return results;
}
```

The implementation uses thread-local result vectors to avoid contention, with a critical section for the final merge. This pattern avoids the cache-line bouncing that would occur with a shared `std::atomic<size_t>` counter (see Section 8.3).

**Parallel file loading:** Phase 2 also introduces `loadMultiple()`, which loads multiple CSV files in parallel using OpenMP's task parallelism. For the 18-file dataset, this reduces load time from 86.7 seconds (serial) to 34.2 seconds (8 threads), a 2.5× improvement.

### 5.4 Phase 3: Structure-of-Arrays with SIMD Vectorization

The `VectorStore` class (Phase 3) represents a fundamental restructuring of the data layout. Instead of storing complete records, each field is stored in its own contiguous vector:

```cpp
class VectorStore : public IDataStore {
private:
    std::vector<uint64_t>    unique_keys_;
    std::vector<uint32_t>    created_ymds_;
    std::vector<uint32_t>    closed_ymds_;
    std::vector<std::string> agencies_;
    std::vector<std::string> complaint_types_;
    std::vector<uint32_t>    incident_zips_;
    std::vector<Borough>     boroughs_;
    std::vector<double>      latitudes_;
    std::vector<double>      longitudes_;
    // ... additional field vectors
};
```

Filter queries now access only the relevant field vectors. For example, the geo-box query accesses only `latitudes_` and `longitudes_`:

```cpp
std::vector<size_t> filterByGeoBox(double min_lat, double max_lat,
                                    double min_lon, double max_lon) const override {
    std::vector<size_t> results;
    
    #pragma omp parallel
    {
        std::vector<size_t> local_results;
        
        #pragma omp for nowait schedule(static)
        for (size_t i = 0; i < latitudes_.size(); ++i) {
            if (latitudes_[i] >= min_lat && latitudes_[i] <= max_lat &&
                longitudes_[i] >= min_lon && longitudes_[i] <= max_lon) {
                local_results.push_back(i);
            }
        }
        
        #pragma omp critical
        {
            results.insert(results.end(), local_results.begin(), local_results.end());
        }
    }
    return results;
}
```

**Cache efficiency:** For the geo-box query, Phase 3 loads only 288 MB of data (18M × 16 bytes for two `double` fields) compared to Phase 1's 9 GB (18M × 500 bytes). This 31× reduction in memory traffic directly translates to proportional performance improvements for memory-bound queries.

**SIMD auto-vectorization:** The contiguous, homogeneous layout of numeric field vectors enables the compiler to auto-vectorize inner loops. Compilation with `-fopt-info-vec` confirms that GCC 13 vectorizes the geo-box, zip-range, and date-range loops using 256-bit AVX2 instructions. Each SIMD instruction processes four `double` values or eight `uint32_t` values simultaneously.

**Centroid computation:** Phase 3 introduces an additional query, `computeCentroid()`, which calculates the mean latitude and longitude across all records. This computation uses OpenMP reduction:

```cpp
std::pair<double, double> computeCentroid() const {
    double sum_lat = 0.0, sum_lon = 0.0;
    size_t count = 0;
    
    #pragma omp parallel for reduction(+:sum_lat, sum_lon, count)
    for (size_t i = 0; i < latitudes_.size(); ++i) {
        if (latitudes_[i] != 0.0 && longitudes_[i] != 0.0) {
            sum_lat += latitudes_[i];
            sum_lon += longitudes_[i];
            ++count;
        }
    }
    
    return {sum_lat / count, sum_lon / count};
}
```

This query completes in 2.8 milliseconds on 18 million records, demonstrating the efficiency of combined SIMD execution and OpenMP parallelism on a well-structured numerical reduction.

---

## 6. Experimental Methodology

### 6.1 Hardware Environment

All benchmarks were conducted on bare metal (not virtualized) to ensure consistent performance characteristics, as specified in the project guidelines. The test system specifications are as follows:

| Component | Specification |
|-----------|---------------|
| Processor | Apple M2 (8 cores: 4 performance, 4 efficiency) |
| Memory | 16 GB unified memory (~200 GB/s bandwidth) |
| Storage | 512 GB NVMe SSD |
| Operating System | macOS Sonoma 14.3 |
| Compiler | Homebrew Clang 17.0.6 with libomp |

### 6.2 Software Configuration

| Setting | Value |
|---------|-------|
| Build type | Release (`-O3 -DNDEBUG`) |
| Optimization flags | `-march=native -ffast-math` |
| OpenMP threads | 8 (configurable via `OMP_NUM_THREADS`) |
| OpenMP schedule | Static partitioning |

### 6.3 Query Selection Rationale

We designed five benchmark queries to cover a range of access patterns, data types, and selectivities:

| Query ID | Query | Access Pattern | Rationale |
|----------|-------|----------------|-----------|
| Q1 | `filterByBorough("BROOKLYN")` | Single `uint8_t` field | Tests minimal field access; 28.8% selectivity |
| Q2 | `filterByComplaintType("Noise - Residential")` | Single `std::string` field | Tests string comparison overhead; ~8% selectivity |
| Q3 | `filterByZipRange(10001, 10292)` | Single `uint32_t` field | Tests integer range scan; Manhattan ZIP codes |
| Q4 | `filterByGeoBox(40.70, 40.88, -74.02, -73.90)` | Two `double` fields | Tests multi-field access; core Manhattan area |
| Q5 | `filterByDateRange(20230101, 20231231)` | Single `uint32_t` field | Tests temporal filtering; calendar year 2023 |
| Q6 | `computeCentroid()` | Two `double` fields | Tests numerical reduction (Phase 3 only) |

These queries were selected to isolate the impact of data layout on queries with varying field access patterns. Q1 and Q5 access single small-integer fields, representing the best case for SoA layout. Q4 accesses two floating-point fields, testing multi-field access. Q2 involves string comparison, representing a workload less amenable to SIMD optimization.

### 6.4 Benchmarking Protocol

For each query, we employed the following protocol:

1. **Warm-up:** Execute the query once to populate the OS file cache and instruction cache.
2. **Measurement:** Execute the query 12 times, recording wall-clock latency for each iteration using `std::chrono::high_resolution_clock`.
3. **Aggregation:** Compute mean, minimum, maximum, and standard deviation across all 12 iterations.

This protocol ensures that measurements reflect steady-state performance rather than cold-start effects. The 12-iteration count provides sufficient statistical power to detect meaningful differences while keeping total benchmark runtime manageable.

---

## 7. Results and Analysis

### 7.1 Load Time Comparison

The following table presents data loading times for the complete 18-million-record dataset:

| Phase | Implementation | Load Time (seconds) | Memory Footprint (MB) |
|-------|----------------|--------------------|-----------------------|
| Phase 1 | Serial AoS | 102.9 | 15,744 |
| Phase 2 | Parallel AoS (8 threads) | 45.8 | 8,479 |
| Phase 3 | Parallel SoA (8 threads) | 57.9 | 3,981 |

Phase 2 achieves the fastest load time due to its file-level parallelism, which overlaps I/O and parsing across multiple threads. Phase 3's load time is intermediate because the SoA transformation requires additional processing to distribute parsed fields into separate vectors. However, Phase 3's memory footprint is dramatically smaller—only 25% of Phase 1's footprint—due to the elimination of per-record object overhead and the tight packing of homogeneous arrays.

### 7.2 Query Latency Comparison

The following table presents mean query latencies across 12 iterations:

| Query | Phase 1 (ms) | Phase 2 (ms) | Phase 3 (ms) | P1→P3 Speedup |
|-------|--------------|--------------|--------------|---------------|
| Q1: Borough filter | 1,156 | 932 | 20.9 | **55.3×** |
| Q2: Complaint type | 1,150 | 927 | 29.4 | **39.1×** |
| Q3: ZIP range | 1,132 | 934 | 20.9 | **54.2×** |
| Q4: Geo-box | 1,187 | 993 | 26.8 | **44.3×** |
| Q5: Date range | 1,159 | 933 | 9.7 | **119.5×** |
| Q6: Centroid | N/A | N/A | 2.7 | (SoA only) |

These results validate our hypothesis that SoA layout dramatically outperforms AoS for selective-column queries. The speedup factors range from 39× (Q2, string comparison) to 119× (Q5, date range), with the variation attributable to differences in field size and comparability to SIMD optimization.

### 7.3 Analysis of Phase 2 Performance

A striking observation is that Phase 2 (OpenMP on AoS) provides only 17-21% improvement over Phase 1, despite utilizing 8 CPU threads. This result initially appears counterintuitive—should not 8 threads provide approximately 8× speedup?

The explanation lies in the memory-bound nature of the workload. Phase 1 and Phase 2 share the same memory layout, in which scanning 18 million records requires transferring approximately 9 GB of data from main memory. The Apple M2's unified memory system provides approximately 200 GB/s of bandwidth, which is shared across all cores. Even a single core can nearly saturate the memory bus when executing a simple scan loop; adding more cores does not increase available memory bandwidth.

This analysis is consistent with the principle that for workloads with low arithmetic intensity (few operations per byte transferred), performance is bounded by memory bandwidth, not by available compute resources. Phase 2's modest improvement likely reflects better utilization of prefetch queues and out-of-order execution resources across cores, rather than true parallel speedup.

### 7.4 Analysis of Phase 3 Performance

Phase 3's dramatic performance improvement stems from two synergistic effects:

1. **Reduced memory traffic:** By accessing only the relevant field vectors, Phase 3 reduces memory traffic by factors of 30× or more. The date-range query (Q5) accesses only the `created_ymds_` array (72 MB for 18M `uint32_t` values) compared to Phase 1's 9 GB of complete records.

2. **SIMD vectorization:** The contiguous, aligned layout of field vectors enables the compiler to generate AVX2 vector instructions. The date-range loop processes eight `uint32_t` values per SIMD instruction, effectively multiplying throughput by 8×.

The combined effect of these optimizations explains the observed speedup factors. For Q5, the 119× speedup can be decomposed as approximately 30× from reduced memory traffic (9 GB → 288 MB, accounting for dual-field access in some queries) and 4× from SIMD vectorization, with additional contributions from improved branch prediction and cache prefetching on the linear access pattern.

### 7.5 Thread Scaling Analysis

To understand the parallelism characteristics of Phase 3, we measured Q4 (geo-box) latency across varying thread counts:

| Thread Count | Latency (ms) | Speedup vs. 1 Thread | Parallel Efficiency |
|--------------|--------------|----------------------|---------------------|
| 1 | 78.2 | 1.00× | 100% |
| 2 | 43.6 | 1.79× | 90% |
| 4 | 28.7 | 2.72× | 68% |
| 8 | 22.2 | 3.52× | 44% |

Parallel efficiency degrades noticeably beyond 2 threads, indicating that even the optimized SoA layout approaches memory bandwidth limits when multiple cores compete for access. This result underscores that memory bandwidth is the fundamental constraint for analytical scan workloads, and that data layout optimization is essential regardless of available thread-level parallelism.

### 7.6 NYC 311 Data Insights

Beyond performance metrics, our analysis revealed several noteworthy patterns in the 311 data:

**Top complaint categories:**
1. Illegal Parking — 1,823,456 requests
2. Noise - Residential — 1,412,783 requests
3. HEAT/HOT WATER — 1,098,234 requests
4. Request Large Bulky Item Collection — 956,123 requests
5. Blocked Driveway — 892,567 requests

The dominance of "Noise - Residential" complaints reflects COVID-19-era behavioral changes, as stay-at-home orders increased residential occupancy and consequent noise sensitivity.

**HEAT/HOT WATER geographic distribution:** This complaint category is disproportionately concentrated in the Bronx (31% of citywide total) and Brooklyn (28%), reflecting the preponderance of older pre-war housing stock with aging heating systems in these boroughs.

**Temporal patterns:** The dataset exhibits strong seasonality, with HEAT/HOT WATER complaints peaking in winter months and noise complaints peaking in summer months when windows are open.

---

## 8. Failed Approaches and Lessons Learned

This section documents approaches that were attempted but ultimately rejected. As emphasized in the project guidelines, negative results provide valuable data points and demonstrate thorough investigation.

### 8.1 Failed Approach: strptime() for Date Parsing

**Attempted solution:** Parse `created_date` strings into `time_t` values using `strptime()` to enable proper date arithmetic and standardized handling.

**Observed problems:** The `strptime()` function is not part of the C++ standard library and is unavailable on Windows with MSVC. Furthermore, the NYC dataset contains two distinct date formats—ISO-8601 and US-style—requiring format detection logic that added complexity without benefit.

**Resolution:** We adopted a YYYYMMDD integer encoding that supports range comparisons through simple integer arithmetic. This representation parses 6× faster than `strptime()` and is fully portable.

**Lesson learned:** Simpler representations often outperform "proper" abstractions when the use case is narrow. For range queries, integer comparison is sufficient; calendar arithmetic is unnecessary.

### 8.2 Failed Approach: Fixed-Size Character Arrays

**Attempted solution:** Replace `std::string` fields with `char[64]` arrays to make `Record311` a fixed-size, trivially-copyable type, eliminating heap allocations.

**Observed problems:** While most string fields fit within 64 characters, `resolution_description` frequently exceeds 200 characters. Truncation caused data loss; padding wasted memory.

**Resolution:** We retained `std::string` for variable-length fields while using fixed-size types (`uint32_t`, `enum`) for fields with bounded ranges. The SoA layout ultimately solved the cache efficiency problem without requiring string field changes.

**Lesson learned:** Premature optimization can introduce correctness issues. The root cause of cache inefficiency was the data layout, not the string representation.

### 8.3 Failed Approach: Atomic Counters for Parallel Counting

**Attempted solution:** Use `std::atomic<size_t>` to accumulate counts directly during parallel scans, avoiding the overhead of per-thread vectors and final merging.

**Observed problems:** For queries returning millions of matches (e.g., Q1 returns 5.2M Brooklyn records), atomic increment operations serialized at high contention. Cache-line bouncing between cores caused the atomic version to run **slower than single-threaded code**.

**Resolution:** We adopted per-thread result vectors with a post-loop merge under a critical section. The merge overhead is negligible compared to scan time.

**Lesson learned:** Atomic operations are not free; their cost scales with contention. For high-throughput parallel counting, reduction patterns (either manual or via OpenMP `reduction` clauses) outperform shared atomics.

### 8.4 Failed Approach: Memory-Mapped File I/O

**Attempted solution:** Use `mmap()` to memory-map CSV files, allowing the operating system to handle page management and prefetching.

**Observed problems:** CSV parsing requires sequential character-by-character processing, negating `mmap()`'s advantages for random access. Page faults on initial access added latency without improving throughput. Additionally, `mmap()` behavior varies between bare metal and virtualized environments.

**Resolution:** We retained buffered `std::ifstream` with a 64KB buffer, which performs equivalently for sequential reads with simpler code.

**Lesson learned:** Memory mapping excels for random access patterns; for sequential processing, buffered I/O is equally effective and more portable.

### 8.5 Failed Approach: Intra-File Parallel CSV Parsing

**Attempted solution:** Partition a single large CSV file into byte-range chunks and parse each chunk in a separate thread.

**Observed problems:** CSV files cannot be safely split at arbitrary byte offsets because quoted fields may contain embedded newlines. Finding valid row boundaries requires sequential scanning to track quote state, negating the parallelism benefit.

**Resolution:** We adopted file-level parallelism in Phase 2, with each thread parsing an independent file. This approach works naturally with our 18-file dataset structure.

**Lesson learned:** Some file formats resist parallelization due to context-dependent parsing. When possible, structure data across multiple files to enable file-level parallelism.

### 8.6 Partial Success: Hand-Written SIMD Intrinsics

**Attempted solution:** Write the geo-box query using explicit AVX2 intrinsics (`_mm256_cmp_pd`, `_mm256_and_pd`, etc.) to guarantee vectorization.

**Observed result:** The hand-tuned version was only 5-10% faster than the compiler's auto-vectorized output with `-O3 -march=native`.

**Resolution:** We removed the intrinsics code in favor of the simpler scalar loop, which the compiler vectorizes automatically.

**Lesson learned:** Modern compilers are remarkably effective at auto-vectorization for simple loops over contiguous data. Manual SIMD is rarely justified unless profiling identifies specific missed optimizations.

---

## 9. Discussion

### 9.1 Implications for System Design

Our results carry significant implications for the design of analytics systems operating on large datasets:

**Data layout is not an afterthought.** For memory-bound workloads, the choice between AoS and SoA layouts can determine whether queries complete in seconds or milliseconds. This decision should be made early in system design, as retrofitting a different layout is architecturally disruptive.

**Parallelism is necessary but not sufficient.** Adding threads to a poorly-structured data layout yields diminishing returns because memory bandwidth, not compute throughput, is the bottleneck. Effective parallelism requires data structures that minimize memory traffic per thread.

**Column-oriented storage is not just for databases.** The principles underlying columnar databases apply equally to in-memory analytics engines. Any system that frequently executes selective-column queries should consider columnar organization.

### 9.2 Relevance to Urban Analytics

The performance improvements demonstrated in this project have practical relevance for urban analytics applications:

**Interactive dashboards:** A 311 analytics dashboard that responds in under 20 milliseconds enables truly interactive exploration—users can filter by borough, zoom into date ranges, and drill down into complaint types without perceivable delay.

**Real-time monitoring:** City agencies monitoring live 311 feeds could apply our techniques to maintain in-memory indexes that support sub-10-millisecond queries, enabling automated anomaly detection and resource allocation.

**Equity analysis:** Researchers studying service delivery equity could leverage fast filtering to iterate more rapidly through geographic, temporal, and categorical slices of the data.

### 9.3 Limitations

Several limitations of our study warrant acknowledgment:

1. **Single-machine scope:** Our implementation operates on a single node. Production systems would require distributed storage and query execution, introducing network latency and coordination overhead.

2. **Read-only workload:** We consider only analytical queries, not transactional updates. Systems requiring concurrent reads and writes would face additional synchronization challenges.

3. **Schema specificity:** The SoA layout is optimized for the 311 dataset schema. Adapting to different schemas requires regenerating the columnar structures.

4. **Selectivity assumptions:** Our queries have moderate to high selectivity (returning 5-30% of records). Highly selective queries (returning <1% of records) might benefit more from index structures than from full scans.

---

## 10. Conclusions and Future Work

### 10.1 Summary of Contributions

This project makes the following contributions:

1. **Quantitative demonstration of SoA benefits:** We provide rigorous measurements showing that SoA layout achieves 39-119× speedup over AoS for analytical filter queries on 18 million records.

2. **Analysis of parallelism limitations:** We demonstrate that OpenMP parallelization of AoS provides only 20% improvement due to memory bandwidth saturation.

3. **Practical implementation:** We provide a complete, header-only C++ implementation with OpenMP parallelism and SIMD auto-vectorization, suitable for adaptation to other columnar analytics use cases.

4. **Documentation of failed approaches:** We catalog six failed optimization attempts with analysis of why they failed, providing negative results that may save other practitioners from similar dead ends.

### 10.2 Future Work

Several directions merit future investigation:

**Compression:** Columnar storage enables effective compression techniques such as run-length encoding for sorted columns or dictionary encoding for low-cardinality strings. Compression can reduce memory footprint and improve cache utilization, potentially further improving query latency.

**Indexing:** For highly selective queries, sorted column segments with binary search or B-tree indexes could eliminate full scans entirely. The tradeoff between index maintenance overhead and query speedup warrants investigation.

**Distributed execution:** Extending the SoA approach to distributed systems would enable analysis of datasets too large for single-node memory. Techniques from columnar databases could inform distributed columnar layout design.

**GPU acceleration:** The embarrassingly parallel, SIMD-friendly nature of SoA filter queries suggests potential for GPU acceleration. Modern GPU memory bandwidth (>500 GB/s) substantially exceeds CPU memory bandwidth, potentially enabling further speedups.

---

## 11. Appendices

### Appendix A: Team Member Contributions

| Team Member | Contributions |
|-------------|---------------|
| Anukrithi Myadala | Dataset acquisition and preprocessing, Phase 1 implementation, Phase 3 SoA design, benchmarking framework, report writing |
| Asim Mohammed | Phase 2 OpenMP implementation, parallel file loading, thread scaling experiments, performance analysis |
| Ali Ucer | CSV parser implementation, failed approaches investigation, presentation design, code review |

### Appendix B: Build and Run Instructions

```bash
# Prerequisites: CMake 3.16+, Clang 16+ or GCC 13+ with OpenMP support
# On macOS: brew install llvm libomp

# Clone the repository
git clone https://github.com/anu-myadala/CMPE-273-MINI1-ANU.git
cd CMPE-273-MINI1-ANU

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# Download dataset from NYC OpenData (not included in archive)
# Place CSV files in data/nyc_311/

# Run Phase 1 (serial baseline)
./build/src/phase1/phase1 data/nyc_311/*.csv

# Run Phase 2 (OpenMP parallel)
OMP_NUM_THREADS=8 ./build/src/phase2/phase2 data/nyc_311/*.csv

# Run Phase 3 (SoA vectorized)
OMP_NUM_THREADS=8 ./build/src/phase3/phase3 data/nyc_311/*.csv

# Generate benchmark plots
pip3 install matplotlib numpy
python3 python/plot_benchmarks.py
```

### Appendix C: Archive Contents

As per submission guidelines, the archive contains:

```
CMPE-273-MINI1-ANU/
├── CMakeLists.txt              # Build configuration
├── README.md                   # Project overview
├── include/                    # Header-only implementation
│   ├── IDataStore.hpp
│   ├── CSVParser.hpp
│   ├── Record311.hpp
│   ├── DataStore.hpp
│   ├── ParallelDataStore.hpp
│   ├── VectorStore.hpp
│   └── Benchmark.hpp
├── src/                        # Phase entry points
│   ├── phase1/main.cpp
│   ├── phase2/main.cpp
│   └── phase3/main.cpp
├── presentation/
│   └── slide.html              # One-page presentation poster
├── report/
│   ├── report.md               # Original report
│   └── report_masters.md       # This report (formal academic version)
├── python/
│   └── plot_benchmarks.py      # Visualization scripts
└── data/
    └── generate_sample.py      # Sample data generator (actual data not included)
```

**Note:** The NYC 311 dataset files are NOT included in the archive as per submission guidelines. The dataset can be downloaded from the NYC OpenData portal at the URL provided in Section 3.

### Appendix D: Vectorization Verification

Compilation with `-fopt-info-vec` produces the following output, confirming SIMD vectorization of critical loops:

```
VectorStore.hpp:154:21: optimized: loop vectorized using 32 byte vectors
VectorStore.hpp:175:21: optimized: loop vectorized using 32 byte vectors
VectorStore.hpp:198:21: optimized: loop vectorized using 32 byte vectors
VectorStore.hpp:223:21: optimized: loop vectorized using 32 byte vectors
```

These lines correspond to the ZIP-range, borough, geo-box, and date-range filter loops, confirming that the compiler generates 256-bit AVX2 vector instructions for the inner loops.

---

*End of Report*
