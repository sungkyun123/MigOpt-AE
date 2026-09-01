# MigOpt

## What is MigOpt?

**MigOpt** is a near-optimal *offline* data placement algorithm for multi-tiered
memory systems (local/remote DRAM and PMEM). It formulates allocation,
promotion, and demotion across tiers as a Minimum-Cost Maximum-Flow (MCMF)
problem and solves it in polynomial time, providing a strong reference point
for analyzing online data placement heuristics. The paper derives four key
observations from MigOpt and builds **MigFlow**, a lightweight online policy,
on top of them.

MigOpt is implemented in a trace-driven simulator over a four-tier H-NUMA
memory system, which enables us to measure the average access latency,
per-tier access ratios, and migration behavior of MigOpt and the online
policies it is compared against.

The paper that introduces MigOpt is currently under submission to
[ACM EuroSys 2027](https://2027.eurosys.org).

## Implementation Overview

MigOpt is implemented in **`migsim`** (`simulator/`), the simulator used in
the paper: it builds the MCMF graph over a trace, computes a per-phase
placement **schedule**, and also contains the AutoTiering/MTM implementations
used in the paper for Fig. 4 and the analysis data behind Fig. 5–7 and Fig. 9.
The comparison itself is done by **`polsim`** (`policy-sim/`), a unified
evaluator that replays a trace under `auto` (AutoTiering), `mtm` (MTM), and
`migopt` (the schedule computed by `migsim`), measuring all three in the
same way.

All techniques run on the same simulated four-tier system: T0 local DRAM /
T1 remote DRAM / T2 local PMEM / T3 remote PMEM with capacity ratio 1:1:4:4
holding 110% of the working set.

For evaluation, we provide one down-sampled trace per workload (roughly 1/25 of
the paper's original workload runs), so every ordering and qualitative
pattern of the paper's simulation figures reproduces on a laptop, while
absolute values differ with scale. Full-scale runs (18M instructions, ~92K
pages per workload) need tens of GB of RAM and hours of MCMF time and are
not part of this artifact.

## Hardware Prerequisites

The hardware requirements for executing the MigOpt artifact are as follows.

* **CPU**: any x86-64 CPU; we recommend **at least 4 cores** for a fast build.

* **DRAM**: the largest run (the MCMF solver on the YCSB-D trace) stays
  under **2 GB** of resident memory.

* **Disk**: less than **500 MB**; the traces are included in the repository
  (~26 MB).

## Software Prerequisites

- **OS**: Linux (tested on **Ubuntu 22.04 and 24.04**)
- **Toolchain**: `g++` with C++17, `cmake` ≥ 3.12
- **Libraries**: `libconfig-dev`
- **Figures**: `python3` with `matplotlib` (only for `make_figures.sh`)

To run the MigOpt artifact, your environment must support **g++**, **cmake**,
and **libconfig**.

The setup process for these components is described below.

## Installation & Compilation

There are three major steps to install and run the MigOpt artifact:

1. Install the dependencies
2. Clone the repository
3. Build the binaries

### 0. Install Dependencies

Install system packages required by the simulators and the figure scripts:

```bash
sudo apt-get install -y build-essential cmake libconfig-dev python3-matplotlib
```

### 1. Clone the Repository

```bash
git clone https://github.com/sungkyun123/MigOpt-AE.git
cd MigOpt-AE
```

### 2. Build

```bash
./build.sh
```

You should see:

```
[1/3] Building MigOpt simulator (migsim)...
[2/3] Building policy simulator (polsim)...
[3/3] Building trace converter...
Build complete.
  - simulator/build/src/migsim
  - policy-sim/polsim
  - policy-sim/convert_trace
```

which confirms that the three binaries are built.

## Execution

To reproduce all results, simply execute the following from the repository
root:

```bash
./run_all.sh       # MigOpt + evaluator on 3 workloads
./make_figures.sh  # renders Fig. 4 - Fig. 9 into results/figures/
```

`run_all.sh` runs everything — for each workload it computes the MigOpt
schedule, runs the figure and analysis runs, and evaluates AutoTiering, MTM,
and the MigOpt schedule replay under the unified evaluator. All results are
written to `results/`.
The full execution takes **10-20 minutes** depending on the CPU (about 20 minutes on our 2.1 GHz evaluation server),
and `make_figures.sh` takes about **15 seconds**.

To run each step separately:

```bash
# 1) MigOpt on one workload -> placement schedule + stats
./simulator/build/src/migsim configs/small-ycsb.cfg

# 2) convert the trace and the schedule into the evaluator's ID space
./policy-sim/convert_trace traces/ycsb.trace \
    results/ycsb.migopt_mode1.sched

# 3) unified evaluator: an online policy, or the MigOpt schedule replay
./policy-sim/polsim auto   traces/ycsb.trace.converted
./policy-sim/polsim migopt traces/ycsb.trace.converted \
    results/ycsb.migopt_mode1.sched.converted
```

## Code Structure

The implementation spans the `simulator` and `policy-sim` directories. Below
is a compact overview of key components.

**MigOpt & Figure Generation (`simulator/`)**

* **`src/opt/migopt.cpp`** – the MCMF formulation and solver (per-page flow
  network over tiers × migration phases).
* **`src/pol/{an,at,mtm}.cpp`** – the AutoNUMA, AutoTiering, and MTM
  implementations used in the paper (Fig. 4).
* **`src/analysis.cpp`** – replays schedules and writes the CSV files used
  for Fig. 5–7 and Fig. 9 (`results/figs-<workload>/`).

**Unified Evaluator (`policy-sim/`)**

* **`main.cpp`** – AutoTiering, MTM, and the MigOpt schedule replay; reports
  per-tier access counts, migration matrices, and demotion analysis
  (age and post-demotion accesses).
* **`convert_trace.cpp`** – renumbers trace pages (and schedule entries)
  into the sequential IDs the evaluator uses.

**Configs, Traces & Plotting**

* **`configs/`** – per-workload configs: `small-*.cfg` for the summary runs,
  `fig4/*.cfg` for the figure replays and analysis runs (16K-instruction
  phase at the paper's migration intensity).
* **`traces/`** – one trace per workload (`ycsb.trace`, `btree.trace`,
  `xsbench.trace`; `R|W <page>` per line), each down-sampled to roughly 1/25
  of the original workload run. Every experiment runs on these three files.
* **`plots/make_figures.py`** – renders Fig. 4 – Fig. 9 in the paper's style.

## Results

During the experiment, you can observe that MigOpt computes a near-optimal
placement schedule per workload, the unified evaluator compares it against
AutoTiering and MTM, and the figure scripts reproduce the paper's analysis.

The following results are from an example run:

* **Experimental Setup**

```
mig_period: 16000, mig_quota: 50
# of instruction: 786303, # of unique_va_num: 3826
Simulation Technique: MigOpt (schedule replay)
```

* **Summary table**: `run_all.sh` ends with one row per (workload,
  technique), all measured by the unified evaluator. What to check, per the
  paper (§4.1):
  - **MigOpt has the lowest average access latency on every workload**
  - **MTM is the worst on YCSB-D; AutoTiering is the worst on B-Tree and
    XSBench**, matching Fig. 4(a)
  - **MTM issues the most migrations** on every workload, dominated by
    demotions (cascading demotion), while AutoTiering stays very low

```
workload   technique  avg_latency(ns)  promotions   demotions    total_mig
ycsb       migopt     106.1            359          2400         2759
ycsb       auto       209.6            382          414          796
ycsb       mtm        237.5            1225         1960         3185
xsbench    migopt     99.0             956          1110         2066
xsbench    auto       158.0            355          378          733
xsbench    mtm        133.4            1025         2527         3552
btree      migopt     102.9            150          489          639
btree      auto       222.2            26           32           58
btree      mtm        213.8            400          777          1177
```

* **Per-technique output**: each `results/<workload>.<technique>.log`
  reports per-tier accesses, the migration matrix, and the demotion analysis
  (per src→dst path: `count / avg page age at demotion / avg accesses within
  the next 5 migration phases`). Example (`ycsb.migopt.log`, the schedule
  replay):

```
[Access Stats]
Tier | Accesses | Latency(ns) | Time(ns)
----------------------------------------
   0 |   660724 |          80 |  52857920
   1 |    44170 |         130 |   5742100
   2 |    73441 |         300 |  22032300
   3 |     7968 |         350 |   2788800
----------------------------------------
Total access time = 83421120 ns

[Migration Matrix] (count)
      →  0      1      2      3
...
Total Migration count: 2759 (Promo: 359/Demo: 2400)
```

* **Figures**: `make_figures.sh` writes `results/figures/fig4.{png,pdf}` …
  `fig9.{png,pdf}`, rendered in the paper's figure style:
  - **Fig. 4** – overall comparison per workload: (a) average access latency,
    (b) per-tier access ratio, (c) migration count
  - **Fig. 5** – where MigOpt allocates new pages over time (per-tier
    allocation ratio per migration phase, with the whole-run average on the
    side)
  - **Fig. 6** – (a) CDF of accesses a page receives right after allocation
    (the allocation burst); (b) ratio of T0-allocated pages demoted soon
    after their burst
  - **Fig. 7** – MigOpt's demotions per src→dst tier path: count, page age
    at demotion, and accesses received after demotion
  - **Fig. 8** – the same demotion analysis for the heuristics (AutoTiering
    and MTM) on YCSB-D
  - **Fig. 9** – MigOpt's promotion picks against pages ranked by recency,
    frequency, benefit, and gain (benefit − α·cost) around the migration
    quota

  Fig. 4(a)/(b) come from separate runs at the paper's migration intensity
  (16K-instruction phases, 160-page quota, `configs/fig4/`); the analysis
  data behind Fig. 5–7/9 comes from the same runs. The summary uses
  the same phase length with a 50-page quota for MigOpt and both
  policies.
