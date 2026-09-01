#!/usr/bin/env bash
# Renders paper-style Fig. 4 - Fig. 9 from run_all.sh outputs into results/figures/.
set -euo pipefail
cd "$(dirname "$0")"

python3 -c "import matplotlib" 2>/dev/null || {
    echo "matplotlib is required: pip3 install matplotlib" >&2; exit 1; }
[ -f results/ycsb.migopt.log ] || {
    echo "No results found; running ./run_all.sh first..."; ./run_all.sh; }

python3 plots/make_figures.py
echo "Figures written to results/figures/ (fig4 ... fig9, png + pdf)"
