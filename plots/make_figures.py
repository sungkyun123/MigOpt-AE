#!/usr/bin/env python3
"""Generates paper-style figures (Fig. 4 - Fig. 9) from run_all.sh outputs.

Inputs (produced by ./run_all.sh):
  results/<workload>.<technique>.log      simulator stdout (fig 4, 8)
  results/figs-<workload>/*.csv           MigOpt analysis CSVs (fig 5, 6, 7, 9)
Outputs:
  results/figures/fig4.png ... fig9.png (+ .pdf)

The figures mirror the *formats* of the paper's Fig. 4-9 on the small-scale
traces; absolute values differ from the paper's full-scale runs by design.
"""
import csv
import os
import re
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "results")
OUT = os.path.join(RES, "figures")

IDEAL_LAT = 80  # ns, all accesses served from T0
PHASE = 16000   # migration-phase length (instructions), as in the configs
QUOTA = 160     # migration quota per phase (analysis runs)


def die(msg):
    sys.stderr.write("error: %s\n" % msg)
    sys.exit(1)


# ---------------------------------------------------------------- log parsing
def parse_polsim_log(path):
    """-> dict(tier_acc[4], total_acc_time, mig_total, mig[4][4], demo{(s,d):(cnt,age,rem)})"""
    d = {"tier_acc": [0] * 4, "mig": [[0] * 4 for _ in range(4)], "demo": {}}
    with open(path) as f:
        txt = f.read()
    for m in re.finditer(r"^\s*([0-3]) \|\s*(\d+) \|", txt, re.M):
        d["tier_acc"][int(m.group(1))] = int(m.group(2))
    m = re.search(r"Total access time = (\d+)", txt)
    d["total_acc_time"] = int(m.group(1)) if m else 0
    m = re.search(r"Total Migration count: (\d+)", txt)
    d["mig_total"] = int(m.group(1)) if m else 0
    for m in re.finditer(r"^src ([0-3])((?:\s+\d+)+)\s*$", txt, re.M):
        src = int(m.group(1))
        cells = re.findall(r"(\d+)", m.group(2))[:4]
        for dst, v in enumerate(cells):
            d["mig"][src][dst] = int(v)
    for m in re.finditer(r"^src ([0-2])((?:\s+->\d+: [\d.]+ / [\d.]+ / [\d.]+)+)", txt, re.M):
        src = int(m.group(1))
        for dm in re.finditer(r"->(\d+): ([\d.]+) / ([\d.]+) / ([\d.]+)", m.group(2)):
            d["demo"][(src, int(dm.group(1)))] = (
                int(dm.group(2)), float(dm.group(3)), float(dm.group(4)))
    return d


def parse_migsim_log(path):
    """-> dict(avg_lat, tier_acc[4], mig_total, mig[4][4])"""
    d = {"tier_acc": [0] * 4, "mig": [[0] * 4 for _ in range(4)]}
    with open(path) as f:
        txt = f.read()
    m = re.search(r"avg access\s*=\s*([\d.]+)", txt)
    ma = re.search(r"Accesses per tier:.*?cnt :\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", txt, re.S)
    if m and ma:
        d["avg_lat"] = float(m.group(1))
        d["tier_acc"] = [int(ma.group(i)) for i in range(1, 5)]
    else:
        # Fallback: per-policy stats block (lat_acc lat_mig lat_alc / access stat)
        mb = re.search(r"lat_acc lat_mig lat_alc\s*\n(\d+)\s+\d+\s+\d+.*?"
                       r"access stat\s*\n(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", txt, re.S)
        if not mb:
            die("no parseable stats in %s" % path)
        d["tier_acc"] = [int(mb.group(i)) for i in range(2, 6)]
        d["avg_lat"] = int(mb.group(1)) / max(1, sum(d["tier_acc"]))
    m = re.search(r"Migrations \(src -> dst\) \[total=(\d+)\]", txt)
    d["mig_total"] = int(m.group(1)) if m else 0
    blk = re.search(r"Migrations \(src -> dst\).*?\n((?:\s*T[0-3]\s*->.*\n){4})", txt)
    if blk:
        for src, line in enumerate(blk.group(1).strip().splitlines()):
            cells = re.findall(r"(\d+)", line.split("->", 1)[1])[:4]
            d["mig"][src] = [int(c) for c in cells]
    return d


def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


# ---------------------------------------------------------------------------
# Figure style: Times New Roman, inward ticks, dashed y-grids, black bar edges
WORKLOADS = ["ycsb", "btree", "xsbench"]          # panel order
WLABEL = {"ycsb": "YCSB-D", "btree": "B-Tree", "xsbench": "XSBench"}
TECH_COLOR = {"auto": "darkseagreen", "mtm": "orange", "migopt": "midnightblue"}
TECH_LABEL = {"auto": "AutoT", "mtm": "MTM", "migopt": "MigOpt"}
FIG4_TECHS = ["auto", "mtm", "migopt"]
TIER_COLORS = ["#e63946", "#f4a261", "#2a9d8f", "#457b9d"]
WCOLOR = {"ycsb": "#e63946", "btree": "#2a9d8f", "xsbench": "#457b9d"}

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "Liberation Serif", "Nimbus Roman", "DejaVu Serif"],
    "font.size": 17,
    "pdf.fonttype": 42, "ps.fonttype": 42,
    "axes.linewidth": 1.3,
    "axes.grid": False,
    "xtick.direction": "in", "ytick.direction": "in",
    "legend.frameon": False,
})


def style_ax(ax, grid_lw=1.2):
    for side in ("top", "bottom", "left", "right"):
        ax.spines[side].set_linewidth(1.3)
    ax.grid(axis="y", zorder=-100, linewidth=grid_lw, linestyle="--")
    ax.set_axisbelow(True)
    ax.tick_params(axis="both", direction="in")


# --------------------------------------------------------------------- fig 4
def fig4(pol, f4):
    fig = plt.figure(figsize=(12.6, 2.1))
    gs = plt.GridSpec(1, 5, width_ratios=[2.6, 1.3, 1.3, 1.3, 2.6], wspace=0.42)
    bw = 0.2
    x = np.arange(len(WORKLOADS))
    off = [-(len(FIG4_TECHS) - 1) * bw / 2 + i * bw for i in range(len(FIG4_TECHS))]

    # (a) average access latency
    ax = fig.add_subplot(gs[0])
    style_ax(ax)
    ax.plot([-5, 100], [IDEAL_LAT, IDEAL_LAT], linewidth=2, color="salmon", ls="--", zorder=5)
    bars = []
    for i, t in enumerate(FIG4_TECHS):
        vals = [f4[w][t]["avg_lat"] for w in WORKLOADS]
        bars.append(ax.bar(x + off[i], vals, bw, color=TECH_COLOR[t], zorder=10,
                           linewidth=1.3, edgecolor="k", label=TECH_LABEL[t]))
    ax.set_xlim(-0.5, 2.5)
    ax.set_xticks(x - 0.1)
    ax.set_xticklabels([WLABEL[w] for w in WORKLOADS], fontsize=15)
    ax.tick_params(axis="x", rotation=20, pad=-4)
    ax.set_yticks([0, 80, 100, 200])
    ax.set_yticklabels(["0", "80", "100", "200"], fontsize=14)
    for lbl in ax.get_yticklabels():
        if lbl.get_text() == "80":
            lbl.set_color("red")
    for gl, y in zip(ax.get_ygridlines(), ax.get_yticks()):
        if y == 80:
            gl.set_visible(False)
    ax.set_ylim(0, 280)
    ax.set_ylabel("Latency (ns)", fontsize=16)
    ax.annotate("Ideal", xy=(1.52, 74), xytext=(1.52, 175),
                arrowprops=dict(arrowstyle="->", color="darkred", lw=2),
                color="darkred", fontsize=14, ha="center")
    ax.legend(ncol=3, fontsize=13, loc="upper center", bbox_to_anchor=(0.5, 1.32),
              columnspacing=0.6, handletextpad=0.25, handlelength=0.8)
    ax.set_title("(a) Average access latency", fontsize=15, y=-0.42)

    # (b) hit ratio per tier -- one sub-panel per workload
    handles = None
    for wi, w in enumerate(WORKLOADS):
        ax = fig.add_subplot(gs[1 + wi])
        style_ax(ax)
        xs = np.arange(len(FIG4_TECHS))
        acc = [f4[w][t]["tier_acc"] for t in FIG4_TECHS]
        fr = np.array([[a[i] / max(1, sum(a)) for i in range(4)] for a in acc])
        base = np.zeros(len(FIG4_TECHS))
        hh = []
        for tier in range(4):
            h = ax.bar(xs, fr[:, tier], bottom=base, width=0.7, zorder=100,
                       edgecolor="k", color=TIER_COLORS[tier], linewidth=1.2)
            base += fr[:, tier]
            hh.append(h)
        if handles is None:
            handles = hh
        ax.set_xticks(xs + 0.55)
        ax.set_xticklabels([TECH_LABEL[t] for t in FIG4_TECHS], fontsize=11.5)
        ax.tick_params(axis="x", rotation=43, pad=-2)
        for lbl in ax.get_xticklabels():
            lbl.set_horizontalalignment("right")
        ax.set_xlim(-0.7, len(FIG4_TECHS) - 1 + 0.7)
        ax.set_ylim(0, 1)
        ax.set_yticks([0, 0.5, 1])
        ax.set_yticklabels(["0", "0.5", "1"] if wi == 0 else [], fontsize=13)
        ax.set_title(WLABEL[w], fontsize=13.5, pad=3)
        if wi == 0:
            ax.set_ylabel("Hit ratio", fontsize=15, labelpad=0)
        if wi == 1:
            ax.text(0.5, -0.52, "(b) Hit ratio for each tier", fontsize=15,
                    ha="center", transform=ax.transAxes)
    fig.legend(handles, ["0", "1", "2", "3"], ncol=4, fontsize=13,
               loc="upper center", bbox_to_anchor=(0.52, 1.19),
               columnspacing=0.7, handletextpad=0.25, handlelength=0.8)
    fig.text(0.40, 1.075, "Tier:", fontsize=14)

    # (c) migration count
    ax = fig.add_subplot(gs[4])
    style_ax(ax)
    for i, t in enumerate(FIG4_TECHS):
        vals = [pol[w][t]["mig_total"] for w in WORKLOADS]
        ax.bar(x + off[i], vals, bw, color=TECH_COLOR[t], zorder=10,
               linewidth=1.3, edgecolor="k", label=TECH_LABEL[t])
    ax.set_xlim(-0.5, 2.5)
    ax.set_xticks(x - 0.1)
    ax.set_xticklabels([WLABEL[w] for w in WORKLOADS], fontsize=15)
    ax.tick_params(axis="x", rotation=20, pad=-4)
    top = max(max(pol[w][t]["mig_total"] for w in WORKLOADS) for t in FIG4_TECHS)
    lim = int(np.ceil(top / 1000) * 1000)
    ax.set_yticks([0, lim // 2, lim])
    ax.set_yticklabels(["0", "%dK" % (lim // 2000) if lim >= 2000 else str(lim // 2),
                        "%dK" % (lim // 1000)], fontsize=14)
    ax.set_ylim(0, lim * 1.05)
    ax.set_ylabel("Count", fontsize=16)
    ax.legend(ncol=3, fontsize=13, loc="upper center", bbox_to_anchor=(0.5, 1.32),
              columnspacing=0.6, handletextpad=0.25, handlelength=0.8)
    ax.set_title("(c) Migration count", fontsize=15, y=-0.42)
    return fig


# --------------------------------------------------------------------- fig 5
def fig5():
    fig = plt.figure(figsize=(10.8, 1.9))
    gs = plt.GridSpec(1, 6, width_ratios=[4.2, 0.9, 4.2, 0.9, 4.2, 0.9], wspace=0.16)
    handles = None
    for wi, w in enumerate(WORKLOADS):
        rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig5_alloc_dist.csv"))
        per = defaultdict(lambda: [0] * 4)
        for r in rows:
            per[int(r["period"])][int(r["tier"])] += int(r["alloc_count"])
        periods = sorted(p for p in per if sum(per[p]) > 0)
        max_p = max(periods) if periods else 1
        ax = fig.add_subplot(gs[wi * 2])
        style_ax(ax, grid_lw=0.7)
        xs = np.array(periods)
        fr = np.array([[per[p][t] / max(1, sum(per[p])) for t in range(4)] for p in periods])
        base = np.zeros(len(periods))
        for tier in range(4):
            ax.bar(xs, fr[:, tier], bottom=base, width=1.0,
                   color=TIER_COLORS[tier], linewidth=0)
            base += fr[:, tier]
        # x-axis spans this workload's full run
        with open("traces/%s.trace" % w) as tf:
            n_inst = sum(1 for _ in tf)
        nper = max(max_p + 1, (n_inst + PHASE - 1) // PHASE)
        ax.set_xlim(-0.9, nper + 0.9)
        ax.set_xticks([0, nper])
        ax.set_xticklabels(["0", "%dK" % round(n_inst / 1000)], fontsize=13)
        ax.set_ylim(0, 1)
        ax.set_yticks([0, 0.5, 1])
        ax.set_yticklabels(["0", "0.5", "1"] if wi == 0 else [], fontsize=13)
        if wi == 0:
            ax.set_ylabel("Allocated\npage ratio", fontsize=14, labelpad=-1)
        ax.set_title(WLABEL[w], fontsize=14.5, x=0.62, pad=3)
        ax.set_xlabel("# of inst.", fontsize=14, labelpad=-1)
        if w == "xsbench" and periods:
            lastp = max(periods)
            if lastp < nper - 6:
                ax.text((lastp + nper) / 2, 0.5, "No\nallocation", ha="center",
                        va="center", fontsize=13, color="dimgray", rotation=90)
        # overall side bar
        axs = fig.add_subplot(gs[wi * 2 + 1])
        for side in ("top", "bottom", "left", "right"):
            axs.spines[side].set_linewidth(1.3)
        axs.tick_params(axis="both", direction="in")
        tot = [sum(per[p][t] for p in periods) for t in range(4)]
        s = max(1, sum(tot))
        b = 0.0
        hh = []
        for tier in range(4):
            h = axs.bar(0, tot[tier] / s, bottom=b, width=0.6,
                        color=TIER_COLORS[tier], edgecolor="k", linewidth=1)
            b += tot[tier] / s
            hh.append(h)
        if handles is None:
            handles = hh
        axs.set_ylim(0, 1)
        axs.set_xlim(-0.8, 0.8)
        axs.set_xticks([])
        axs.set_yticks([0, 0.5, 1])
        axs.set_yticklabels([])
    fig.legend(handles, ["0", "1", "2", "3"], ncol=4, fontsize=13.5,
               loc="upper center", bbox_to_anchor=(0.53, 1.18),
               columnspacing=0.7, handletextpad=0.25, handlelength=0.8)
    fig.text(0.415, 1.055, "Tier:", fontsize=14)
    return fig


# --------------------------------------------------------------------- fig 6
def fig6():
    fig = plt.figure(figsize=(7.6, 1.9))
    gs = plt.GridSpec(1, 2, width_ratios=[1.25, 1], wspace=0.32)

    # (a) CDF of access count within the next 100 references
    ax = fig.add_subplot(gs[0])
    style_ax(ax, grid_lw=1.0)
    meds = {}
    for w in WORKLOADS:
        rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig7a_burst_cdf.csv"))
        vals = sorted(int(r["n_access_in_next_100"]) for r in rows)
        if not vals:
            continue
        n = len(vals)
        marker = {"ycsb": "o", "btree": "x", "xsbench": "v"}[w]
        ax.plot(vals, [i / n for i in range(1, n + 1)], lw=2.2,
                color=WCOLOR[w], label=WLABEL[w], marker=marker,
                markevery=0.3, markerfacecolor="none", markersize=9,
                markeredgewidth=2, zorder=10)
        meds[w] = vals[n // 2]
    for w, m in meds.items():
        ax.plot([m, m], [0, 0.5], ls="--", lw=1.5, color=WCOLOR[w])
        ax.text(m, -0.17, str(m), color=WCOLOR[w], fontsize=13,
                ha="center", fontweight="bold")
    ax.plot([0, 100], [0.5, 0.5], ls="--", lw=1.0, color="gray", zorder=-50)
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 1)
    ax.set_xticks([0, 100])
    ax.set_xticklabels(["0", "100"], fontsize=13)
    ax.set_yticks([0, 0.5, 1.0])
    ax.set_yticklabels(["0", "0.5", "1.0"], fontsize=13)
    ax.set_xlabel("# of accesses", fontsize=15, labelpad=16)
    ax.set_ylabel("CDF", fontsize=15)
    ax.legend(fontsize=12, ncol=3, loc="upper center", bbox_to_anchor=(0.5, 1.3),
              columnspacing=0.6, handletextpad=0.3, handlelength=0.9)
    ax.set_title("(a)", fontsize=15, y=-0.62)

    # (b) ratio of T0-allocated pages demoted after their burst
    ax = fig.add_subplot(gs[1])
    for side in ("top", "bottom", "left", "right"):
        ax.spines[side].set_linewidth(1.3)
    ax.tick_params(axis="both", direction="in")
    ax.grid(axis="x", zorder=-100, linewidth=1.0, linestyle="--")
    ax.set_axisbelow(True)
    names, ratios = [], []
    for w in WORKLOADS:
        alloc_rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig5_alloc_dist.csv"))
        alloc_t0 = sum(int(r["alloc_count"]) for r in alloc_rows if r["tier"] == "0")
        demo_rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig6_demo.csv"))
        demo = sum(1 for r in demo_rows
                   if r["src_tier"] == "0" and int(r["age_at_demo"]) <= PHASE)
        names.append(WLABEL[w])
        ratios.append(min(1.0, demo / alloc_t0) if alloc_t0 else 0.0)
    ax.barh(range(len(names)), ratios, 0.62, color=TIER_COLORS[3],
            edgecolor="k", linewidth=1.2, zorder=10)
    ax.set_yticks(range(len(names)))
    ax.set_yticklabels(names, fontsize=13)
    ax.invert_yaxis()
    ax.set_xlim(0, 1)
    ax.set_xticks([0, 0.5, 1.0])
    ax.set_xticklabels(["0.0", "0.5", "1.0"], fontsize=13)
    ax.set_xlabel("Ratio", fontsize=15, labelpad=8)
    ax.set_title("(b)", fontsize=15, y=-0.62)
    return fig


# ---------------------------------------------------------- matrix rendering
CMAPS = {}


def get_cmaps():
    if not CMAPS:
        import matplotlib.colors  # noqa
        for name, c in (("count", "coolwarm"), ("age", "summer"), ("remain", "bone")):
            cm = plt.get_cmap(c).copy() if hasattr(plt.get_cmap(c), "copy") else plt.get_cmap(c)
            cm.set_bad(color="darkgray")
            CMAPS[name] = cm
    return CMAPS


def demo_matrix(ax, cells, metric, vmax, fmt):
    import matplotlib.colors as mcolors
    cm = get_cmaps()[metric]
    data = np.full((3, 3), np.nan)
    for (src, dst), v in cells.items():
        if 0 <= src < 3 and 1 <= dst < 4 and dst > src:
            data[src][dst - 1] = v
    masked = np.ma.masked_invalid(data)
    # lower-left (invalid path) cells render as the bad color
    for src in range(3):
        for dst in range(1, 4):
            if dst <= src:
                masked[src, dst - 1] = np.ma.masked
    im = ax.imshow(masked, cmap=cm, origin="upper", vmin=0, vmax=max(vmax, 1))
    for y in range(4):
        ax.hlines(y - 0.5, -0.5, 2.5, color="black", linewidth=1)
        ax.vlines(y - 0.5, -0.5, 2.5, color="black", linewidth=1)
    norm = mcolors.Normalize(vmin=0, vmax=max(vmax, 1))
    for src in range(3):
        for dst in range(1, 4):
            if dst <= src:
                continue
            v = data[src][dst - 1]
            if np.isnan(v):
                continue
            rgba = cm(norm(v))
            lum = 0.299 * rgba[0] + 0.587 * rgba[1] + 0.114 * rgba[2]
            ax.text(dst - 1, src, fmt(v), ha="center", va="center", fontsize=11,
                    color="white" if lum < 0.53 else "black")
    ax.set_xticks([0, 1, 2])
    ax.set_xticklabels(["$T_1$", "$T_2$", "$T_3$"], fontsize=12)
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["$T_0$", "$T_1$", "$T_2$"], fontsize=12)
    ax.tick_params(length=0)
    ax.grid(False)
    return im


def compact(v):
    if v >= 1e6:
        return "%.1fM" % (v / 1e6)
    if v >= 1e3:
        return "%.0fK" % (v / 1e3)
    return "%.0f" % v if v == int(v) else "%.1f" % v


# --------------------------------------------------------------------- fig 7
REMAIN_WIN_PHASES = 5  # remain accesses counted within this many phases


def load_positions(w):
    pos = defaultdict(list)
    path = os.path.join(ROOT, "traces", "%s.trace" % w)
    if not os.path.exists(path):
        return None
    with open(path) as f:
        for i, line in enumerate(f):
            pos[line.split()[1]].append(i)
    return pos


def fig7():
    import bisect
    per = {}
    for w in WORKLOADS:
        rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig6_demo.csv"))
        pos = load_positions(w)
        # per-page placement timeline from the MigOpt schedule, to stop
        # counting once a demoted page is promoted back up
        timeline = defaultdict(list)
        sched_path = os.path.join(RES, "%s.migopt_mode1.sched" % w)
        if os.path.exists(sched_path):
            with open(sched_path) as f:
                for line in f:
                    p = line.split()
                    if len(p) >= 4 and p[0] == "A":
                        timeline[p[2]].append((int(p[1]), int(p[3])))
        agg = defaultdict(lambda: [0, 0, 0])
        for r in rows:
            k = (int(r["src_tier"]), int(r["dst_tier"]))
            agg[k][0] += 1
            agg[k][1] += int(r["age_at_demo"])
            # a page cools if it sees no accesses soon after demotion; count
            # its remaining accesses within the next REMAIN_WIN_PHASES phases,
            # but only while it actually stays demoted
            if pos is not None and r["addr"] in pos:
                demo_pos = (int(r["period"]) + 1) * PHASE
                end = demo_pos + REMAIN_WIN_PHASES * PHASE
                for t, tier in timeline.get(r["addr"], ()):
                    if t > demo_pos and tier < int(r["dst_tier"]):
                        end = min(end, t)
                        break
                ps = pos[r["addr"]]
                lo = bisect.bisect_right(ps, demo_pos)
                hi = bisect.bisect_right(ps, end)
                agg[k][2] += hi - lo
            else:
                agg[k][2] += int(r["remain_access"])
        per[w] = agg
    metrics = [("count", "(a) Demotion count", lambda a: a[0], compact),
               ("age", "(b) Age of demoted pages", lambda a: a[1] / a[0] if a[0] else None, compact),
               ("remain", "(c) Remain access after demotion", lambda a: a[2] / a[0] if a[0] else None,
                lambda v: "%.0f" % v)]
    fig, axs = plt.subplots(3, 3, figsize=(6.4, 8.6))
    plt.subplots_adjust(hspace=0.78, wspace=0.12)
    for mi, (mk, mtitle, get, fmt) in enumerate(metrics):
        cellsets = []
        vmax = 1
        for w in WORKLOADS:
            cells = {k: get(a) for k, a in per[w].items() if get(a) is not None}
            cellsets.append(cells)
            if cells:
                vmax = max(vmax, max(cells.values()))
        ims = []
        for wi, w in enumerate(WORKLOADS):
            ax = axs[mi][wi]
            ims.append(demo_matrix(ax, cellsets[wi], mk, vmax, fmt))
            ax.set_title(WLABEL[w], fontsize=14, pad=6)
            if wi == 0:
                ax.set_ylabel("Source", fontsize=12)
            else:
                ax.set_yticklabels([])
        cbar = fig.colorbar(ims[-1], ax=list(axs[mi]), fraction=0.046, pad=0.02)
        cbar.set_ticks([0, max(vmax, 1)])
        cbar.set_ticklabels(["0", compact(max(vmax, 1))])
        cbar.ax.tick_params(labelsize=11)
        axs[mi][1].text(0.5, -0.38, "Destination", fontsize=12, ha="center",
                        transform=axs[mi][1].transAxes)
        axs[mi][1].text(0.5, -0.60, mtitle, fontsize=14.5, ha="center",
                        transform=axs[mi][1].transAxes)
    return fig


# --------------------------------------------------------------------- fig 8
def fig8(pol):
    w = "ycsb"
    metrics = [("count", "(a) Demotion count", 0, compact),
               ("age", "(b) Age of demoted pages", 1, compact),
               ("remain", "(c) Remain access", 2, lambda v: "%.0f" % v)]
    fig = plt.figure(figsize=(10.6, 2.1))
    gs = plt.GridSpec(1, 8, width_ratios=[1, 1, 0.12, 1, 1, 0.12, 1, 1], wspace=0.1)
    axs = [fig.add_subplot(gs[c]) for c in (0, 1, 3, 4, 6, 7)]
    for mi, (mk, mtitle, idx, fmt) in enumerate(metrics):
        cellsets = []
        vmax = 1
        for tech in ("auto", "mtm"):
            cells = {k: v[idx] for k, v in pol[w][tech]["demo"].items() if v[0] > 0}
            cellsets.append(cells)
            if cells:
                vmax = max(vmax, max(cells.values()))
        for ti, tech in enumerate(("auto", "mtm")):
            ax = axs[mi * 2 + ti]
            demo_matrix(ax, cellsets[ti], mk, vmax, fmt)
            ax.set_title(TECH_LABEL[tech], fontsize=13.5, pad=4)
            if mi * 2 + ti != 0:
                ax.set_yticklabels([])
        l = axs[mi * 2].get_position()
        r = axs[mi * 2 + 1].get_position()
        fig.text((l.x0 + r.x1) / 2, -0.08, "Destination", fontsize=12, ha="center")
        fig.text((l.x0 + r.x1) / 2, -0.24, mtitle, fontsize=14, ha="center")
    axs[0].set_ylabel("Source", fontsize=12)
    return fig


# --------------------------------------------------------------------- fig 9
def fig9():
    w = "ycsb"
    rows = read_csv(os.path.join(RES, "figs-%s" % w, "fig9_scores.csv"))
    if not rows:
        return None
    from collections import defaultdict as dd
    all_periods = sorted({int(r["period"]) for r in rows})
    lo = all_periods[len(all_periods) // 10]
    hi = all_periods[(len(all_periods) * 17) // 20]
    by_period = dd(list)
    for r in rows:
        pd = int(r["period"])
        if lo <= pd <= hi:
            by_period[pd].append(r)

    # use a mid-run phase with enough promotions to look at
    def n_picks(rs):
        seen = {}
        for r in rs:
            a = r["addr"]
            seen[a] = max(seen.get(a, 0), int(r["migopt_promoted_here"]))
        return sum(seen.values())

    eligible = [p for p in sorted(by_period) if n_picks(by_period[p]) >= 20]
    if not eligible:
        return None
    period = eligible[(len(eligible) - 1) // 2]

    pages = {}
    for r in rows:
        if int(r["period"]) != period:
            continue
        a = r["addr"]
        cur = pages.get(a)
        ben, cost = float(r["benefit"]), float(r["cost"])
        rec = {"recency": float(r["recency"]), "freq": float(r["freq"]),
               "benefit": ben, "cost": cost, "cur": int(r["cur_tier"]),
               "pick": int(r["migopt_promoted_here"])}
        if cur is None:
            pages[a] = rec
        else:
            for k in ("freq", "benefit"):
                cur[k] = max(cur[k], rec[k])
            cur["cost"] = min(cur["cost"], rec["cost"])
            cur["recency"] = min(cur["recency"], rec["recency"])
            cur["pick"] = max(cur["pick"], rec["pick"])

    # page access frequency over the profiling window
    trace = os.path.join(ROOT, "traces", "ycsb.trace")
    if os.path.exists(trace):
        horizon = (period + 1) * PHASE + PHASE
        fut = {}
        with open(trace) as f:
            for i, line in enumerate(f):
                if i >= horizon:
                    break
                a = line.split()[1]
                fut[a] = fut.get(a, 0) + 1
        for a, p in pages.items():
            if a in fut:
                p["freq"] = fut[a]

    plist = list(pages.values())
    npick = sum(p["pick"] for p in plist)
    quota = QUOTA
    # gain = benefit - alpha * migration cost
    bens = sorted((p["benefit"] for p in plist), reverse=True)
    med_cost = sorted(p["cost"] for p in plist)[len(plist) // 2] or 1.0
    anchor = bens[min(600, len(bens) - 1)] or 1.0
    cscale = anchor / (3 * med_cost)
    for p in plist:
        for a in (3, 4, 5):
            p["gain_a%d" % a] = p["benefit"] - a * med_cost * cscale

    ranks = [("(a) Recency", "recency", False), ("(b) Frequency", "freq", True),
             ("(c) Benefit", "benefit", True), (r"(d) Gain ($\alpha$=3)", "gain_a3", True),
             (r"(e) Gain ($\alpha$=4)", "gain_a4", True), (r"(f) Gain ($\alpha$=5)", "gain_a5", True)]
    B = 25
    SHOW = 40
    fig, axes = plt.subplots(2, 3, figsize=(10.2, 4.4))
    plt.subplots_adjust(hspace=0.85, wspace=0.22)
    for ax, (title, key, desc) in zip(axes.flat, ranks):
        for side in ("top", "bottom", "left", "right"):
            ax.spines[side].set_linewidth(1.3)
        ax.tick_params(axis="both", direction="in")
        order = sorted(plist, key=lambda p: p[key], reverse=desc)
        nb = min(SHOW, (len(order) + B - 1) // B)
        picks = [0] * nb
        for i, p in enumerate(order[:nb * B]):
            if p["pick"]:
                picks[i // B] += 1
        xs = np.arange(nb)
        ax.bar(xs, [min(B, len(order) - b * B) for b in xs], 1.0,
               color="lightgray", linewidth=0, zorder=5, label="Non-picks")
        ax.bar(xs, picks, 1.0, color="royalblue", linewidth=0, zorder=6,
               label="MigOpt picks")
        ax.axvline(quota / B - 0.5, color="forestgreen", ls="--", lw=1.8, zorder=8)
        if key.startswith("gain"):
            neg = next((i for i, p in enumerate(order) if p[key] <= 0), None)
            if neg is not None and neg / B < nb:
                ax.axvspan(neg / B - 0.5, nb - 0.5, color="dimgray", alpha=0.42,
                           lw=0, zorder=7)
                ax.text(min(neg / B + 0.4, nb - 13), B * 0.82, r"gain $\leq$ 0",
                        fontsize=12, color="darkred", zorder=9)
        ax.set_title(title, fontsize=15, y=-0.62)
        ax.set_ylim(0, B)
        ax.set_yticks([0, 25])
        ax.set_xlim(-0.5, nb - 0.5)
        ax.set_xticks([0, quota / B - 0.5, nb / 2, nb - 1])
        ax.set_xticklabels(["1", str(quota), str(int(nb / 2 * B)), str(nb * B)], fontsize=12)
        if title[1] in "ad":
            ax.set_ylabel("# of pages", fontsize=14)
        ax.set_xlabel("Page rank", fontsize=13, labelpad=-1)
    handles = [Patch(facecolor="royalblue", label="MigOpt picks (%d)" % npick),
               Patch(facecolor="lightgray", label="Non-picks"),
               plt.Line2D([], [], color="forestgreen", ls="--", lw=1.8,
                          label="Migration quota (%d)" % quota)]
    fig.legend(handles=handles, ncol=3, loc="upper center",
               bbox_to_anchor=(0.5, 1.04), fontsize=13)
    return fig


# ---------------------------------------------------------------------- main
def main():
    os.makedirs(OUT, exist_ok=True)
    pol, f4 = {}, {}
    for w in WORKLOADS:
        pol[w] = {}
        for t in ["migopt", "auto", "mtm"]:
            p = os.path.join(RES, "%s.%s.log" % (w, t))
            if os.path.exists(p):
                pol[w][t] = parse_polsim_log(p)
        f4[w] = {}
        for t in ["at", "mtm", "migopt"]:
            f4[w]["auto" if t == "at" else t] = parse_migsim_log(
                os.path.join(RES, "fig4-%s.%s.log" % (w, t)))

    made = []
    for name, fn in [("fig4", lambda: fig4(pol, f4)), ("fig5", fig5), ("fig6", fig6),
                     ("fig7", fig7), ("fig8", lambda: fig8(pol)), ("fig9", fig9)]:
        fig = fn()
        if fig is None:
            print("skip %s (no data)" % name)
            continue
        for ext in ("png", "pdf"):
            fig.savefig(os.path.join(OUT, "%s.%s" % (name, ext)),
                        dpi=170, bbox_inches="tight", facecolor="white")
        plt.close(fig)
        made.append(name)
        print("wrote results/figures/%s.png" % name)
    print("done: %s" % ", ".join(made))


if __name__ == "__main__":
    main()
