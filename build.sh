#!/usr/bin/env bash
# Builds both simulators. Run from anywhere; artifacts stay inside the repo.
set -euo pipefail
cd "$(dirname "$0")"

echo "[1/3] Building MigOpt simulator (migsim)..."
cmake -S simulator -B simulator/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build simulator/build -j"$(nproc)" > simulator/build/build.log 2>&1 \
    || { cat simulator/build/build.log; exit 1; }
tail -1 simulator/build/build.log

echo "[2/3] Building policy simulator (polsim)..."
g++ -O2 -std=c++17 -o policy-sim/polsim policy-sim/main.cpp

echo "[3/3] Building trace converter..."
g++ -O2 -std=c++17 -o policy-sim/convert_trace policy-sim/convert_trace.cpp

echo "Build complete."
echo "  - simulator/build/src/migsim"
echo "  - policy-sim/polsim"
echo "  - policy-sim/convert_trace"
