#!/usr/bin/env bash
# Runs every technique on every workload:
#   - migopt      : offline near-optimal placement via MCMF (simulator/)
#   - auto, mtm   : online policies (policy-sim/)
# Logs go to results/<workload>.<technique>.log; a summary prints at the end.
set -uo pipefail
cd "$(dirname "$0")"

MIGSIM=simulator/build/src/migsim
POLSIM=policy-sim/polsim
CONVERT=policy-sim/convert_trace
WORKLOADS=(ycsb xsbench btree)
TECHS=(auto mtm)

[ -x "$MIGSIM" ] && [ -x "$POLSIM" ] || { echo "Binaries missing; run ./build.sh first." >&2; exit 1; }
mkdir -p results

fail=()
for w in "${WORKLOADS[@]}"; do
    trace=traces/$w.trace
    sched="results/$w.migopt_mode1.sched"

    echo
    echo "============ [$w] MigOpt (offline MCMF -> placement schedule) ============"
    rm -rf results/figs
    if ! "$MIGSIM" "configs/small-$w.cfg" >"results/$w.migsim.log" 2>&1; then
        echo "  -> FAILED (see results/$w.migsim.log)"; fail+=("$w/migsim")
    fi
    # convert the trace and the MigOpt schedule into the evaluator's
    # dense-ID space
    "$CONVERT" "$trace" "$sched" >/dev/null

    for t in migopt "${TECHS[@]}"; do
        echo
        echo "==================== [$w] $t ===================="
        if [ "$t" = migopt ]; then
            ok=0; "$POLSIM" migopt "$trace.converted" "$sched.converted" \
                >"results/$w.migopt.log" 2>&1 || ok=$?
        else
            ok=0; "$POLSIM" "$t" "$trace.converted" >"results/$w.$t.log" 2>&1 || ok=$?
        fi
        if [ "$ok" != 0 ]; then
            echo "  -> FAILED (see results/$w.$t.log)"; fail+=("$w/$t")
        else
            tail -n 12 "results/$w.$t.log"
        fi
    done
done

echo
echo "==================== Figure runs (paper migration intensity) ===================="
# Fig. 4(a)/(b) replay the paper's AutoTiering/MTM/MigOpt implementations
# (built into migsim) at the paper's 1% migration intensity; the MigOpt run
# also writes the analysis CSVs behind Fig. 5-7 and Fig. 9.
for w in "${WORKLOADS[@]}"; do
    for pol in at mtm migopt; do
        log="results/fig4-$w.$pol.log"
        rm -rf results/figs
        "$MIGSIM" "configs/fig4/$w-$pol.cfg" >"$log" 2>&1 || true
        if grep -qE "avg access|lat_acc" "$log"; then
            echo "  fig4 $w/$pol: ok"
        else
            echo "  fig4 $w/$pol: FAILED (see $log)"; fail+=("fig4-$w/$pol")
        fi
        if [ "$pol" = migopt ] && [ -d results/figs ]; then
            rm -rf "results/figs-$w"; mv results/figs "results/figs-$w"
        fi
    done
done

echo
echo "======================================================================"
echo " Summary (full logs in results/)"
echo "======================================================================"
printf "%-10s %-10s %-16s %-12s %-12s %-10s\n" "workload" "technique" "avg_latency(ns)" "promotions" "demotions" "total_mig"
for w in "${WORKLOADS[@]}"; do
    for t in migopt "${TECHS[@]}"; do
        log="results/$w.$t.log"
        [ -f "$log" ] || continue
        acc=$(grep -m1 -oP 'Total access time = \K[0-9]+' "$log" || true)
        n=$(grep -m1 -oP '# of instruction: \K[0-9]+' "$log" || true)
        mig=$(grep -m1 -oP 'Total Migration count: \K[0-9]+' "$log" || true)
        promo=$(grep -m1 -oP 'Promo: \K[0-9]+' "$log" || true)
        demo=$(grep -m1 -oP 'Demo: \K[0-9]+' "$log" || true)
        if [ -n "$acc" ] && [ -n "$n" ] && [ "$n" != 0 ]; then
            avg=$(awk -v a="$acc" -v n="$n" 'BEGIN{printf "%.1f", a/n}')
        else
            avg="n/a"
        fi
        printf "%-10s %-10s %-16s %-12s %-12s %-10s\n" "$w" "$t" "$avg" "${promo:-n/a}" "${demo:-n/a}" "${mig:-n/a}"
    done
done

if [ "${#fail[@]}" -gt 0 ]; then
    echo
    echo "FAILED runs: ${fail[*]}" >&2
    exit 1
fi
echo
echo "All runs completed successfully."
