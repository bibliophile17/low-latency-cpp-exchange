#!/usr/bin/env python3
"""Visualize Google Benchmark JSON output produced by exchange_benchmarks.

Usage:
    python3 scripts/plot_benchmarks.py [path/to/results.json] [--out out.png]

If no path is given, defaults to benchmarks/results/sample_run.json (a
real benchmark run captured on the development machine; regenerate it
yourself with:

    ./build/exchange_benchmarks --benchmark_out=results.json \\
        --benchmark_out_format=json

Numbers in the committed sample file are real measurements from one
run on one machine -- see docs/performance.md for the exact environment
they were captured on. They are NOT a performance guarantee for any
other machine; re-run locally before citing numbers in an interview.

Requires: matplotlib (pip install matplotlib --break-system-packages)
"""
import argparse
import json
import sys
from pathlib import Path


def load_benchmarks(path: Path):
    with open(path) as f:
        data = json.load(f)
    return data.get("context", {}), data.get("benchmarks", [])


def is_throughput_benchmark(name: str) -> bool:
    return "EndToEnd" in name or "Throughput" in name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", nargs="?",
                         default=str(Path(__file__).resolve().parent.parent / "benchmarks" / "results" / "sample_run.json"),
                         help="Path to a Google Benchmark JSON output file")
    parser.add_argument("--out", default="benchmark_report.png",
                         help="Output image path (default: benchmark_report.png)")
    args = parser.parse_args()

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is required: pip install matplotlib --break-system-packages", file=sys.stderr)
        sys.exit(1)

    results_path = Path(args.results)
    if not results_path.exists():
        print(f"Results file not found: {results_path}", file=sys.stderr)
        sys.exit(1)

    context, benchmarks = load_benchmarks(results_path)
    if not benchmarks:
        print("No benchmark entries found in results file.", file=sys.stderr)
        sys.exit(1)

    latency_entries = [b for b in benchmarks if not is_throughput_benchmark(b["name"])]
    throughput_entries = [b for b in benchmarks if is_throughput_benchmark(b["name"])]

    fig, axes = plt.subplots(1, 2 if throughput_entries else 1, figsize=(14, 6))
    if not throughput_entries:
        axes = [axes]

    # --- Latency panel (nanoseconds per operation, log scale) ---
    ax = axes[0]
    names = [b["name"] for b in latency_entries]
    times = [b["real_time"] for b in latency_entries]
    ax.barh(names, times, color="#3b6fa0")
    ax.set_xscale("log")
    ax.set_xlabel("Time per operation (ns, log scale)")
    ax.set_title("Per-operation latency")
    ax.invert_yaxis()
    for i, t in enumerate(times):
        ax.text(t, i, f"  {t:.2f} ns", va="center", fontsize=8)

    # --- Throughput panel (items/sec, for end-to-end / queue benchmarks) ---
    if throughput_entries:
        ax2 = axes[1]
        # Group by base benchmark family name (strip the /<arg> suffix)
        grouped = {}
        for b in throughput_entries:
            base = b["name"].split("/")[0]
            items_per_sec = None
            # Google Benchmark stores user counters (like items_per_second)
            # as top-level keys when reported via SetItemsProcessed +
            # --benchmark_counters_tabular, or as "items_per_second" for
            # some configurations; fall back to computing it manually.
            if "items_per_second" in b:
                items_per_sec = b["items_per_second"]
            grouped.setdefault(base, []).append((b["name"], items_per_sec))

        labels = []
        values = []
        for base, entries in grouped.items():
            for name, v in entries:
                if v is not None:
                    labels.append(name)
                    values.append(v)

        if values:
            ax2.barh(labels, values, color="#a05a3b")
            ax2.set_xlabel("Throughput (items/sec)")
            ax2.set_title("Throughput benchmarks")
            ax2.invert_yaxis()
            for i, v in enumerate(values):
                ax2.text(v, i, f"  {v:,.0f}/s", va="center", fontsize=8)
        else:
            ax2.axis("off")
            ax2.text(0.5, 0.5, "No throughput counters found", ha="center")

    host = context.get("host_name", "unknown host")
    cpus = context.get("num_cpus", "?")
    mhz = context.get("mhz_per_cpu", "?")
    fig.suptitle(f"Matching engine benchmark results — {host} ({cpus} CPU(s) @ {mhz} MHz)\n"
                 f"Source: {results_path.name}", fontsize=10)
    fig.tight_layout(rect=[0, 0, 1, 0.93])
    fig.savefig(args.out, dpi=150)
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
