# CMPE-273-MINI1-ANU

## NYC 311 Service Requests — High-Performance In-Memory Analytics

**Dataset:** [NYC 311 Service Requests from 2020 to Present](https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2020-to-Present/erm2-nwe9)

---

### Build

Requires: g++ ≥ 13 (or clang ≥ 16), CMake ≥ 3.16, OpenMP.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run

```bash
# Phase 1 — Serial AoS baseline
./build/src/phase1/phase1  <311_data.csv>

# Phase 2 — OpenMP parallel AoS (pass multiple yearly files to use loadMultiple)
OMP_NUM_THREADS=8 ./build/src/phase2/phase2  file1.csv [file2.csv ...]

# Phase 3 — SoA vectorized + OpenMP
OMP_NUM_THREADS=8 ./build/src/phase3/phase3  file1.csv [file2.csv ...]
```

Each phase writes `phaseN_results.csv` with benchmark statistics.

### Plot Results

```bash
pip3 install matplotlib numpy
python3 python/plot_benchmarks.py   # writes charts to ./plots/
```

### Smoke Test (no full dataset needed)

```bash
python3 data/generate_sample.py 100000 /tmp/test.csv
./build/src/phase1/phase1 /tmp/test.csv
./build/src/phase2/phase2 /tmp/test.csv
./build/src/phase3/phase3 /tmp/test.csv
```

### Repository Layout

```
include/
  IDataStore.hpp         abstract query interface (virtual/facade)
  CSVParser.hpp          template CSV parser (quoted-field safe)
  Record311.hpp          NYC 311 record — primitive-typed fields + fromFields()
  DataStore.hpp          Phase 1: serial AoS  std::vector<Record311>
  ParallelDataStore.hpp  Phase 2: OpenMP AoS
  VectorStore.hpp        Phase 3: SoA vectorized std::vector<field>[]
  Benchmark.hpp          timer, statistics, CSV output
src/phase1/main.cpp      Phase 1 program
src/phase2/main.cpp      Phase 2 program
src/phase3/main.cpp      Phase 3 program
data/generate_sample.py  synthetic 311 CSV generator for testing
python/plot_benchmarks.py benchmark comparison charts
report/report.md         full research report
presentation/slide.html  one-page poster slide
```
