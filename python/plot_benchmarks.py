#!/usr/bin/env python3
"""
plot_benchmarks.py — reads phase1_results.csv, phase2_results.csv,
phase3_results.csv produced by the C++ programs and generates comparison
charts suitable for the report.

Usage:
    python3 python/plot_benchmarks.py
 
Output files (in ./plots/):
    query_comparison.png  — grouped bar chart, mean query time per phase
    speedup.png           — speedup of Phase 2 and 3 relative to Phase 1
    load_time.png         — bar chart of load times (fill manually below)
    memory.png            — bar chart of peak memory per phase (fill manually)
"""

import os
import csv
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# ---------------------------------------------------------------------------
# Configuration — update these with actual values from your runs
# ---------------------------------------------------------------------------
LOAD_TIMES_SEC = {
    "Phase 1 (Serial)":  0.0,   # fill in after running phase1
    "Phase 2 (OpenMP)":  0.0,   # fill in after running phase2
    "Phase 3 (SoA)":     0.0,   # fill in after running phase3
}
MEMORY_MB = {
    "Phase 1 (Serial)":  0,     # fill in from RSS delta output
    "Phase 2 (OpenMP)":  0,
    "Phase 3 (SoA)":     0,
}

PHASE_LABELS  = ["Phase 1\n(Serial AoS)", "Phase 2\n(OpenMP AoS)", "Phase 3\n(SoA)"]
PHASE_COLORS  = ["#4C72B0", "#DD8452", "#55A868"]
PHASE_FILES   = ["phase1_results.csv", "phase2_results.csv", "phase3_results.csv"]

os.makedirs("plots", exist_ok=True)


def read_results(path: str) -> dict:
    """Returns {label: mean_ms} from a benchmark CSV."""
    results = {}
    if not os.path.exists(path):
        print(f"  [warn] {path} not found — skipping")
        return results
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            results[row["label"]] = float(row["mean_ms"])
    return results


def plot_query_comparison(data: list[dict]) -> None:
    """Grouped bar chart: query label vs mean_ms, grouped by phase."""
    # Collect union of all query labels
    labels = []
    for d in data:
        for k in d:
            if k not in labels:
                labels.append(k)

    if not labels:
        print("No benchmark data found — skipping query_comparison.png")
        return

    n_labels  = len(labels)
    n_phases  = len(data)
    x         = np.arange(n_labels)
    width     = 0.25
    offsets   = [(i - n_phases / 2 + 0.5) * width for i in range(n_phases)]

    fig, ax = plt.subplots(figsize=(12, 6))
    for i, (phase_data, color, plabel) in enumerate(zip(data, PHASE_COLORS, PHASE_LABELS)):
        means = [phase_data.get(lbl, 0.0) for lbl in labels]
        bars  = ax.bar(x + offsets[i], means, width, label=plabel.replace("\n", " "),
                       color=color, edgecolor="white", linewidth=0.6)
        for bar, val in zip(bars, means):
            if val > 0:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.5,
                        f"{val:.1f}", ha="center", va="bottom", fontsize=7)

    # Shorten labels for the axis
    short = [l.replace("Q1_borough_BROOKLYN", "Q1\nBorough")
              .replace("Q2_complaint_Noise_Residential", "Q2\nComplaint")
              .replace("Q3_zip_range_Bronx", "Q3\nZip Range")
              .replace("Q4_geobox_Manhattan", "Q4\nGeo Box")
              .replace("Q5_date_range_2022", "Q5\nDate Range")
              .replace("Q6_centroid_reduction", "Q6\nCentroid")
             for l in labels]

    ax.set_xticks(x)
    ax.set_xticklabels(short, fontsize=9)
    ax.set_ylabel("Mean query time (ms)")
    ax.set_title("Query Performance: Phase 1 vs Phase 2 vs Phase 3\n"
                 "NYC 311 Service Requests 2020–2024")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig("plots/query_comparison.png", dpi=150)
    print("Saved plots/query_comparison.png")
    plt.close(fig)


def plot_speedup(data: list[dict]) -> None:
    """Speedup of Phase 2 and Phase 3 relative to Phase 1 baseline."""
    if not data[0]:
        print("Phase 1 data missing — skipping speedup.png")
        return

    labels = list(data[0].keys())
    if not labels:
        return

    fig, ax = plt.subplots(figsize=(10, 5))
    x      = np.arange(len(labels))
    width  = 0.35

    for i, (phase_data, color, plabel) in enumerate(zip(data[1:], PHASE_COLORS[1:], PHASE_LABELS[1:]), 1):
        speedups = []
        for lbl in labels:
            base = data[0].get(lbl, 0.0)
            cur  = phase_data.get(lbl, 0.0)
            speedups.append(base / cur if cur > 0 and base > 0 else 0.0)
        offset = (i - 1.5) * width
        bars = ax.bar(x + offset, speedups, width, label=plabel.replace("\n", " "),
                      color=color, edgecolor="white", linewidth=0.6)
        for bar, val in zip(bars, speedups):
            if val > 0:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.02,
                        f"{val:.1f}×", ha="center", va="bottom", fontsize=8)

    ax.axhline(1.0, color="black", linestyle="--", linewidth=0.8, label="Baseline (Phase 1)")
    ax.set_xticks(x)
    ax.set_xticklabels([f"Q{i+1}" for i in range(len(labels))], fontsize=10)
    ax.set_ylabel("Speedup relative to Phase 1")
    ax.set_title("Speedup Over Serial Baseline\n(higher is better)")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig("plots/speedup.png", dpi=150)
    print("Saved plots/speedup.png")
    plt.close(fig)


def plot_load_times() -> None:
    labels = list(LOAD_TIMES_SEC.keys())
    times  = list(LOAD_TIMES_SEC.values())
    if all(t == 0.0 for t in times):
        print("Load times not filled in — skipping load_time.png")
        return

    fig, ax = plt.subplots(figsize=(7, 4))
    bars = ax.bar(labels, times, color=PHASE_COLORS, edgecolor="white")
    for bar, val in zip(bars, times):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.3,
                f"{val:.1f}s", ha="center", va="bottom")
    ax.set_ylabel("Load time (seconds)")
    ax.set_title("Data Load Time per Phase\n(12 GB NYC 311 dataset)")
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig("plots/load_time.png", dpi=150)
    print("Saved plots/load_time.png")
    plt.close(fig)


def plot_memory() -> None:
    labels = list(MEMORY_MB.keys())
    mems   = list(MEMORY_MB.values())
    if all(m == 0 for m in mems):
        print("Memory values not filled in — skipping memory.png")
        return

    fig, ax = plt.subplots(figsize=(7, 4))
    bars = ax.bar(labels, mems, color=PHASE_COLORS, edgecolor="white")
    for bar, val in zip(bars, mems):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 10,
                f"{val} MB", ha="center", va="bottom")
    ax.set_ylabel("Peak RSS (MB)")
    ax.set_title("Peak Resident Memory per Phase")
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig("plots/memory.png", dpi=150)
    print("Saved plots/memory.png")
    plt.close(fig)


def main():
    print("Reading benchmark CSVs...")
    data = [read_results(f) for f in PHASE_FILES]

    plot_query_comparison(data)
    plot_speedup(data)
    plot_load_times()
    plot_memory()
    print("Done.  Charts written to ./plots/")


if __name__ == "__main__":
    main()
