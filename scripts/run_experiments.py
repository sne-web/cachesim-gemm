#!/usr/bin/env python3
"""Run the CacheSim experiments and draw the graphs.

Two sweeps are run by calling the `cachesim` program many times in parallel:

1. Grid sweep (fixed matrix size): cache size x associativity x replacement
   policy x workload (naive, and tiled with several tile sizes).
2. Matrix-size sweep (fixed L1-like cache): how naive and tiled multiplication
   behave as the matrices outgrow the cache.

Raw numbers go to CSV files and the graphs to PNG files in the output folder.

Usage:
    python scripts/run_experiments.py              # full run (a few minutes)
    python scripts/run_experiments.py --quick      # small smoke-test run
    python scripts/run_experiments.py --plots-only # redraw from existing CSVs
"""

import argparse
import csv
import io
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# --- What to sweep ----------------------------------------------------------

KIB = 1024
GRID_N = 200
GRID_CACHE_SIZES = [KIB << i for i in range(13)]  # 1 KiB ... 4 MiB
ASSOCIATIVITIES = ["1", "2", "4", "8", "full"]
POLICIES = ["lru", "fifo"]
TILE_SIZES = [4, 8, 16, 32, 64]
MATRIX_SIZES = [50, 100, 200, 300, 400, 500, 600]

# The "reference" cache: a typical CPU L1 data cache.
L1 = {"size": 32 * KIB, "assoc": "8", "policy": "lru"}
BLOCK_BYTES = 64

GRID_CSV = "sweep_grid.csv"
SIZE_CSV = "matrix_size_sweep.csv"
SUMMARY = "summary.txt"

EXTRA_COLUMNS = ["assoc"]  # added in front of cachesim's own CSV columns

# --- Running the simulator --------------------------------------------------


def find_cachesim(build_dir):
    for name in ("cachesim.exe", "cachesim"):
        for candidate in (build_dir / name, build_dir / "Release" / name):
            if candidate.is_file():
                return candidate
    sys.exit(f"error: cachesim not found in '{build_dir}'. Build the project first (see README.md).")


def run_one(cachesim, job):
    """Runs one simulation and returns its CSV row as a dict of strings."""
    command = [
        str(cachesim), "--csv",
        "--size", str(job["size"]), "--block", str(BLOCK_BYTES),
        "--assoc", job["assoc"], "--policy", job["policy"],
        "--workload", job["workload"], "--n", str(job["n"]),
    ]
    if job["workload"] == "tiled":
        command += ["--tile", str(job["tile"])]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    row = next(csv.DictReader(io.StringIO(result.stdout)))
    row["assoc"] = job["assoc"]
    return row


def run_jobs(cachesim, jobs, workers, label):
    start = time.time()
    rows = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for done, row in enumerate(pool.map(lambda job: run_one(cachesim, job), jobs), start=1):
            rows.append(row)
            if done % 50 == 0 or done == len(jobs):
                print(f"  {label}: {done}/{len(jobs)} runs ({time.time() - start:.0f}s)", flush=True)
    return rows


def workloads(tile_sizes):
    return [("naive", 0)] + [("tiled", tile) for tile in tile_sizes]


def grid_jobs(n, cache_sizes, tile_sizes):
    return [
        {"size": size, "assoc": assoc, "policy": policy, "workload": workload, "tile": tile, "n": n}
        for size in cache_sizes
        for assoc in ASSOCIATIVITIES
        for policy in POLICIES
        for workload, tile in workloads(tile_sizes)
    ]


def size_jobs(matrix_sizes, tile_sizes):
    return [
        {**L1, "workload": workload, "tile": tile, "n": n}
        for n in matrix_sizes
        for workload, tile in workloads(tile_sizes)
    ]


def write_csv(path, rows):
    columns = EXTRA_COLUMNS + [name for name in rows[0] if name not in EXTRA_COLUMNS]
    with open(path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    if not path.is_file():
        sys.exit(f"error: '{path}' not found. Run without --plots-only first.")
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


# --- Looking results up -----------------------------------------------------


def select(rows, **wanted):
    """Rows whose columns equal the given values (compared as strings)."""
    return [row for row in rows if all(row[key] == str(value) for key, value in wanted.items())]


def one(rows, **wanted):
    matches = select(rows, **wanted)
    if len(matches) != 1:
        raise LookupError(f"expected exactly one row for {wanted}, found {len(matches)}")
    return matches[0]


def miss_percent(row):
    return 100.0 * int(row["misses"]) / int(row["accesses"])


def size_label(size_bytes):
    size_bytes = int(size_bytes)
    return f"{size_bytes // (1024 * 1024)}M" if size_bytes >= 1024 * 1024 else f"{size_bytes // 1024}K"


def best_tile(rows, **wanted):
    """The tiled row with the lowest miss rate among rows matching `wanted`."""
    return min(select(rows, workload="tiled", **wanted), key=miss_percent)


# --- Graphs -----------------------------------------------------------------

# Colours follow the thing being drawn, in every figure: naive is always
# orange, tiled is always blue.
NAIVE = "#eb6834"
TILED = "#2a78d6"
TILED_RAMP = ["#86b6ef", "#2a78d6", "#104281"]  # light -> dark, for ordered series
SURFACE = "#fcfcfb"
TEXT = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID = "#e4e3df"


def style_axes(ax, title, xlabel, ylabel):
    ax.set_title(title, loc="left", fontsize=12.5, fontweight="bold", color=TEXT, pad=12)
    ax.set_xlabel(xlabel, color=TEXT_SECONDARY)
    ax.set_ylabel(ylabel, color=TEXT_SECONDARY)
    ax.set_facecolor(SURFACE)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_SECONDARY, length=0)


def new_figure(plt, columns=1):
    fig, axes = plt.subplots(1, columns, figsize=(7.6 if columns == 1 else 11.5, 4.6))
    fig.patch.set_facecolor(SURFACE)
    return fig, axes


def save(fig, out_dir, name):
    fig.tight_layout()
    fig.savefig(out_dir / name, dpi=160, facecolor=fig.get_facecolor())
    print(f"  wrote {name}")


def label_point(ax, x, y, text, dx=0, dy=8, ha="center"):
    ax.annotate(text, (x, y), textcoords="offset points", xytext=(dx, dy), ha=ha, fontsize=8.5, color=TEXT)


def plot_matrix_size(plt, out_dir, rows):
    """Headline graph: miss rate as the matrices outgrow an L1-sized cache."""
    sizes = sorted({int(row["n"]) for row in rows})
    tile = int(best_tile(rows, n=sizes[-1])["tile"])
    naive = [miss_percent(one(rows, workload="naive", n=n)) for n in sizes]
    tiled = [miss_percent(one(rows, workload="tiled", tile=tile, n=n)) for n in sizes]

    fig, ax = new_figure(plt)
    ax.plot(sizes, naive, color=NAIVE, linewidth=2, marker="o", markersize=6, label="Naive (i-j-k loops)")
    ax.plot(sizes, tiled, color=TILED, linewidth=2, marker="s", markersize=6, label=f"Tiled ({tile}x{tile} tiles)")
    for n, value in zip(sizes[1:], naive[1:]):
        label_point(ax, n, value, f"{value:.1f}%")
    label_point(ax, sizes[-1], tiled[-1], f"{tiled[-1]:.2f}%")
    style_axes(ax, "Tiling keeps the miss rate flat as matrices outgrow the cache",
               "Matrix dimension N  (two N x N matrices of doubles; 32 KiB, 8-way, 64-byte-block LRU cache)",
               "Cache miss rate (%)")
    ax.set_xticks(sizes)
    ax.set_ylim(0, max(naive) * 1.15)
    ax.legend(frameon=False, loc="upper left", labelcolor=TEXT)
    save(fig, out_dir, "miss_rate_vs_matrix_size.png")


def plot_cache_size(plt, out_dir, rows):
    """Miss rate against cache size, naive versus tiled, on a log scale."""
    n = rows[0]["n"]
    sizes = sorted({int(row["cache_bytes"]) for row in rows})
    tile = int(best_tile(rows, cache_bytes=L1["size"], assoc=L1["assoc"], policy="LRU")["tile"])
    common = {"assoc": L1["assoc"], "policy": "LRU"}
    naive = [miss_percent(one(rows, workload="naive", cache_bytes=s, **common)) for s in sizes]
    tiled = [miss_percent(one(rows, workload="tiled", tile=tile, cache_bytes=s, **common)) for s in sizes]

    fig, ax = new_figure(plt)
    positions = range(len(sizes))
    ax.plot(positions, naive, color=NAIVE, linewidth=2, marker="o", markersize=6, label="Naive (i-j-k loops)")
    ax.plot(positions, tiled, color=TILED, linewidth=2, marker="s", markersize=6, label=f"Tiled ({tile}x{tile} tiles)")
    ax.set_yscale("log")
    style_axes(ax, f"Tiled code needs a far smaller cache for the same miss rate (N={n})",
               "Cache size (8-way, 64-byte blocks, LRU)", "Cache miss rate (%, log scale)")
    ax.set_xticks(list(positions))
    ax.set_xticklabels([size_label(s) for s in sizes])
    ax.set_yticks([0.01, 0.1, 1, 10, 100])
    ax.set_yticklabels(["0.01", "0.1", "1", "10", "100"])
    ax.minorticks_off()
    ax.legend(frameon=False, loc="lower left", labelcolor=TEXT)
    save(fig, out_dir, "miss_rate_vs_cache_size.png")


def plot_tile_size(plt, out_dir, rows):
    """Miss rate against tile size for three cache sizes: shows the sweet spot."""
    n = rows[0]["n"]
    tiles = sorted({int(row["tile"]) for row in rows if row["workload"] == "tiled"})
    cache_sizes = [size for size in (8 * KIB, 32 * KIB, 128 * KIB)
                   if select(rows, cache_bytes=size)]
    common = {"assoc": L1["assoc"], "policy": "LRU"}

    fig, ax = new_figure(plt)
    positions = list(range(len(tiles)))
    for size, colour in zip(cache_sizes, TILED_RAMP[-len(cache_sizes):]):
        values = [miss_percent(one(rows, workload="tiled", tile=t, cache_bytes=size, **common)) for t in tiles]
        best = min(range(len(values)), key=values.__getitem__)
        ax.plot(positions, values, color=colour, linewidth=2, marker="s", markersize=6,
                label=f"Tiled, {size_label(size)} cache (best: T={tiles[best]}, {values[best]:.2f}%)")
    naive = miss_percent(one(rows, workload="naive", cache_bytes=L1["size"], **common))
    ax.axhline(naive, color=NAIVE, linewidth=2, linestyle=(0, (5, 3)),
               label=f"Naive, {size_label(L1['size'])} cache ({naive:.2f}%)")
    style_axes(ax, f"The best tile is the largest one that still fits in the cache (N={n})",
               "Tile size T  (each tile is T x T doubles)", "Cache miss rate (%)")
    ax.set_xticks(positions)
    ax.set_xticklabels([str(t) for t in tiles])
    ax.set_ylim(bottom=0)
    ax.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.17), labelcolor=TEXT, ncol=2)
    save(fig, out_dir, "miss_rate_vs_tile_size.png")


def plot_associativity(plt, out_dir, rows):
    """Miss rate against associativity for LRU and FIFO; one panel per workload."""
    n = rows[0]["n"]
    tile = int(best_tile(rows, cache_bytes=L1["size"], assoc=L1["assoc"], policy="LRU")["tile"])
    panels = [("naive", 0, NAIVE, "Naive"), ("tiled", tile, TILED, f"Tiled ({tile}x{tile} tiles)")]

    fig, axes = new_figure(plt, columns=2)
    positions = list(range(len(ASSOCIATIVITIES)))
    for ax, (workload, tile_size, colour, name) in zip(axes, panels):
        for policy, dashes, marker, alpha in (("LRU", "-", "o", 1.0), ("FIFO", (0, (4, 2)), "D", 0.6)):
            values = [miss_percent(one(rows, workload=workload, tile=tile_size, cache_bytes=L1["size"],
                                       assoc=assoc, policy=policy)) for assoc in ASSOCIATIVITIES]
            ax.plot(positions, values, color=colour, alpha=alpha, linewidth=2, linestyle=dashes, marker=marker,
                    markersize=6, label=policy)
            if policy == "LRU":
                label_point(ax, positions[0], values[0], f"{values[0]:.2f}%", dx=8, dy=4, ha="left")
                label_point(ax, positions[-1], values[-1], f"{values[-1]:.2f}%", dy=-14)
        style_axes(ax, name, "Associativity (blocks per set)", "Cache miss rate (%)")
        ax.set_xticks(positions)
        ax.set_xticklabels(["1\n(direct)", "2", "4", "8", "full"])
        ax.set_ylim(bottom=0)
        ax.legend(frameon=False, loc="upper right", labelcolor=TEXT, title="Replacement policy")
    fig.suptitle(f"Associativity removes conflict misses; LRU edges out FIFO  "
                 f"({size_label(L1['size'])} cache, N={n})", x=0.012, ha="left", fontsize=12.5,
                 fontweight="bold", color=TEXT)
    save(fig, out_dir, "miss_rate_vs_associativity.png")


def make_plots(out_dir, grid_rows, size_rows):
    import matplotlib
    matplotlib.use("Agg")  # draw to files; no display needed
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10})
    plot_matrix_size(plt, out_dir, size_rows)
    plot_cache_size(plt, out_dir, grid_rows)
    plot_tile_size(plt, out_dir, grid_rows)
    plot_associativity(plt, out_dir, grid_rows)
    plt.close("all")


# --- Text summary -----------------------------------------------------------


def write_summary(out_dir, grid_rows, size_rows):
    lines = ["CacheSim experiment summary", "===========================", ""]
    l1 = f"{size_label(L1['size'])}iB, {L1['assoc']}-way, {BLOCK_BYTES}-byte blocks, LRU"

    lines.append(f"Matrix-size sweep on an L1-like cache ({l1})")
    lines.append(f"{'N':>5} {'naive miss%':>12} {'best tile':>10} {'tiled miss%':>12} {'fewer misses':>13}")
    for n in sorted({int(row["n"]) for row in size_rows}):
        naive = one(size_rows, workload="naive", n=n)
        tiled = best_tile(size_rows, n=n)
        ratio = int(naive["misses"]) / int(tiled["misses"])
        lines.append(f"{n:>5} {miss_percent(naive):>11.2f}% {tiled['tile']:>10} {miss_percent(tiled):>11.2f}% "
                     f"{ratio:>12.1f}x")

    n = grid_rows[0]["n"]
    common = {"cache_bytes": L1["size"], "assoc": L1["assoc"]}
    lines += ["", f"Grid sweep, N={n}, at the same L1-like cache"]
    naive = one(grid_rows, workload="naive", policy="LRU", **common)
    lines.append(f"  naive               : {miss_percent(naive):.2f}% miss rate, {naive['write_backs']} write-backs")
    for row in sorted(select(grid_rows, workload="tiled", policy="LRU", **common), key=lambda r: int(r["tile"])):
        lines.append(f"  tiled, tile={row['tile']:<7}: {miss_percent(row):.2f}% miss rate, "
                     f"{row['write_backs']} write-backs")

    tile = best_tile(grid_rows, policy="LRU", **common)["tile"]
    lines += ["", f"Associativity and policy, N={n}, {size_label(L1['size'])}iB cache (miss rate %)"]
    lines.append(f"{'assoc':>6} {'naive LRU':>10} {'naive FIFO':>11} {'tiled LRU':>10} {'tiled FIFO':>11}"
                 f"   (tile={tile})")
    for assoc in ASSOCIATIVITIES:
        cells = [miss_percent(one(grid_rows, workload=w, tile=t, policy=p, cache_bytes=L1["size"], assoc=assoc))
                 for w, t in (("naive", 0), ("tiled", tile)) for p in ("LRU", "FIFO")]
        lines.append(f"{assoc:>6} {cells[0]:>10.3f} {cells[1]:>11.3f} {cells[2]:>10.3f} {cells[3]:>11.3f}")

    lines += ["", f"Cache size, N={n}, 8-way LRU (miss rate %)"]
    lines.append(f"{'size':>6} {'naive':>9} {'tiled':>9}   (tile={tile})")
    for size in sorted({int(row["cache_bytes"]) for row in grid_rows}):
        key = {"cache_bytes": size, "assoc": L1["assoc"], "policy": "LRU"}
        lines.append(f"{size_label(size):>6} {miss_percent(one(grid_rows, workload='naive', **key)):>9.3f} "
                     f"{miss_percent(one(grid_rows, workload='tiled', tile=tile, **key)):>9.3f}")

    lru_wins = fifo_wins = ties = 0
    for row in select(grid_rows, policy="LRU"):
        other = one(grid_rows, policy="FIFO", **{k: row[k] for k in ("workload", "tile", "cache_bytes", "assoc")})
        difference = int(row["misses"]) - int(other["misses"])
        lru_wins += difference < 0
        fifo_wins += difference > 0
        ties += difference == 0
    lines += ["", f"LRU vs FIFO over all {lru_wins + fifo_wins + ties} grid configurations: "
                  f"LRU fewer misses in {lru_wins}, FIFO fewer in {fifo_wins}, tied in {ties}"]

    total = sum(int(row["accesses"]) for row in grid_rows + size_rows)
    lines += ["", f"Total: {len(grid_rows) + len(size_rows)} simulations, {total:,} simulated memory accesses"]

    text = "\n".join(lines) + "\n"
    (out_dir / SUMMARY).write_text(text)
    print("\n" + text)


# --- Entry point ------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description="Run CacheSim sweeps and draw graphs.")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build", help="folder containing cachesim")
    parser.add_argument("--out-dir", type=Path, default=REPO_ROOT / "results", help="where CSVs and PNGs go")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4, help="simulations to run in parallel")
    parser.add_argument("--quick", action="store_true", help="much smaller sweep, for a fast smoke test")
    parser.add_argument("--plots-only", action="store_true", help="skip simulation; redraw from existing CSVs")
    args = parser.parse_args()

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.plots_only:
        grid_rows = read_csv(out_dir / GRID_CSV)
        size_rows = read_csv(out_dir / SIZE_CSV)
    else:
        cachesim = find_cachesim(args.build_dir)
        if args.quick:
            grid = grid_jobs(48, [KIB << i for i in range(0, 9, 2)] + [8 * KIB, 32 * KIB, 128 * KIB], [4, 8, 16])
            sizes = size_jobs([16, 32, 48, 64], [4, 8, 16])
        else:
            grid = grid_jobs(GRID_N, GRID_CACHE_SIZES, TILE_SIZES)
            sizes = size_jobs(MATRIX_SIZES, TILE_SIZES)
        print(f"Running {len(grid) + len(sizes)} simulations with {args.jobs} parallel jobs...")
        grid_rows = run_jobs(cachesim, grid, args.jobs, "grid sweep")
        size_rows = run_jobs(cachesim, sizes, args.jobs, "matrix-size sweep")
        write_csv(out_dir / GRID_CSV, grid_rows)
        write_csv(out_dir / SIZE_CSV, size_rows)
        print(f"  wrote {GRID_CSV}, {SIZE_CSV}")

    make_plots(out_dir, grid_rows, size_rows)
    write_summary(out_dir, grid_rows, size_rows)


if __name__ == "__main__":
    main()
