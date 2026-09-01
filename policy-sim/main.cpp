// polsim: unified policy evaluator over a four-tier memory system.
//
// Replays a page-granularity trace under one of three techniques with
// identical accounting:
//   auto    AutoTiering (hint-fault promotion + lazy reclaim daemon)
//   mtm     MTM (sampled histogram ranking + cascading demotion)
//   migopt  replay of the placement schedule computed by MigOpt (migsim)
//
// Usage:
//   ./polsim <auto|mtm> <trace.converted>
//   ./polsim migopt <trace.converted> <schedule.converted>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <iomanip>
#include <vector>
#include <map>

#include "simul.h"

using namespace std;

int check_instruction_num(FILE *input) {
    int count = 0;
    char op[2], addr[20];

    while (fscanf(input, "%s %s", op, addr) == 2) {
        count++;
    }

    rewind(input);
    return count;
}

int calc_unique_va_num(FILE *input) {
    char op[2], addr[20];
    unordered_set<uint64_t> va_pages;

    while (fscanf(input, "%s %s", op, addr) == 2) {
        uint64_t va;
        stringstream ss;
        ss << hex << addr;
        ss >> va;
        va_pages.insert(va);
    }

    rewind(input);
    return va_pages.size();
}

int set_system_param(SimulSetup *setup, FILE *input) {
    setup->instruction_num = check_instruction_num(input);
    setup->unique_va_num = calc_unique_va_num(input);

    // Four tiers at a 1:1:4:4 capacity ratio holding 110% of the working set
    int va = setup->unique_va_num;
    int total_cap = static_cast<int>(va * 1.1);
    setup->tier_cap[0] = total_cap / 10;
    setup->tier_cap[1] = total_cap / 10;
    setup->tier_cap[2] = (total_cap * 4) / 10;
    setup->tier_cap[3] = (total_cap * 4) / 10;

    // Access latencies: T0 local DRAM / T1 remote DRAM / T2 local PMEM /
    // T3 remote PMEM
    setup->tier_load_lat[0] = 80;
    setup->tier_load_lat[1] = 130;
    setup->tier_load_lat[2] = 300;
    setup->tier_load_lat[3] = 350;

    // 4KB page-migration latencies per tier
    setup->tier_4KB_R_lat[0] = 386;
    setup->tier_4KB_R_lat[1] = 434;
    setup->tier_4KB_R_lat[2] = 523;
    setup->tier_4KB_R_lat[3] = 847;

    setup->tier_4KB_W_lat[0] = 386;
    setup->tier_4KB_W_lat[1] = 434;
    setup->tier_4KB_W_lat[2] = 523;
    setup->tier_4KB_W_lat[3] = 847;

    setup->kmig_period = 16000;
    setup->auto_reclaim_low = 0.05;
    setup->mig_quota = 50;
    setup->mig_budget = 50;
    setup->inst = 1;

    // Optional overrides for sensitivity experiments
    const char *env;
    if ((env = getenv("POLSIM_MIG_PERIOD")) != NULL) setup->kmig_period = atoi(env);
    if ((env = getenv("POLSIM_MIG_QUOTA")) != NULL) { setup->mig_quota = atoi(env); setup->mig_budget = atoi(env); }
    printf("mig_period: %d, mig_quota: %d\n", setup->kmig_period, setup->mig_quota);
    printf("# of instruction: %d, # of unique_va_num: %d\n", setup->instruction_num, setup->unique_va_num);
    return 0;
}

int build_simul_system(SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, char *tech) {
    int va_num = setup->unique_va_num;

    sysstruct->mtable = (int *)malloc(sizeof(int) * va_num);
    if (!sysstruct->mtable) {
        perror("malloc mtable");
        return -1;
    }
    memset(sysstruct->mtable, -1, sizeof(int) * va_num);

    sysstruct->alloc_order = (int *)malloc(sizeof(int) * 4);

    if (strcmp(tech, "mtm") == 0) {
        printf("Simulation Technique: MTM\n");
        // MTM allocates into the capacity tier first; the fast tiers are
        // filled by promotion
        sysstruct->alloc_order[0] = 2;
        sysstruct->alloc_order[1] = 0;
        sysstruct->alloc_order[2] = 1;
        sysstruct->alloc_order[3] = 3;
    }
    else {
        if (strcmp(tech, "migopt") == 0) printf("Simulation Technique: MigOpt (schedule replay)\n");
        if (strcmp(tech, "auto") == 0) printf("Simulation Technique: AutoTiering\n");
        // AutoTiering allocates local-node first (local DRAM, local PMEM,
        // then the remote node)
        sysstruct->alloc_order[0] = 0;
        sysstruct->alloc_order[1] = 2;
        sysstruct->alloc_order[2] = 1;
        sysstruct->alloc_order[3] = 3;
    }

    sysstruct->access_cnt = (int *)malloc(sizeof(int) * va_num);
    sysstruct->fault_access_cnt = (int *)malloc(sizeof(int) * va_num);
    memset(sysstruct->access_cnt, 0, sizeof(int) * va_num);
    memset(sysstruct->fault_access_cnt, 0, sizeof(int) * va_num);

    sysstruct->reference_bit = (int *)calloc(va_num, sizeof(int));
    if (!sysstruct->reference_bit) {
        perror("calloc reference_bit");
        return -1;
    }

    sysstruct->mig_info = (int **)malloc(sizeof(int *) * 4);
    for (int i = 0; i < 4; ++i) {
        sysstruct->mig_info[i] = (int *)calloc(4, sizeof(int));
        if (!sysstruct->mig_info[i]) {
            perror("calloc mig_info[i]");
            return -1;
        }
    }

    sysstruct->access_info = (int *)calloc(4, sizeof(int));
    if (!sysstruct->access_info) {
        perror("calloc access_info");
        return -1;
    }

    sysstruct->alloc_info = (int *)calloc(4, sizeof(int));
    if (!sysstruct->alloc_info) {
        perror("calloc alloc_info");
        return -1;
    }

    // Tier structures: physical slot numbers are contiguous across tiers
    int va_base = 0;
    for (int i = 0; i < 4; ++i) {
        tiers[i] = Tier(setup->tier_cap[i]);

        for (int j = 0; j < setup->tier_cap[i]; ++j) {
            tiers[i].free_list.push(va_base + j);
        }

        va_base += setup->tier_cap[i];
    }

    return 0;
}

static void lru_touch(Tier& tier, int va) {
    tier.access(va);
}

static inline bool has_spare_free(const Tier& t, const SimulSetup* setup, int tier_id, double ratio) {
    size_t thresh = static_cast<size_t>(setup->tier_cap[tier_id] * ratio);
    return t.free_list.size() > thresh;
}

// --- Demotion analysis: age at demotion / post-demotion accesses ------------
static std::vector<long long> g_alloc_inst;      // per-VA allocation timestamp
static std::vector<long long> g_demo_inst;       // per-VA last demotion timestamp
static std::vector<signed char> g_demo_src, g_demo_dst; // last demotion path, -1 = none
#define REMAIN_WINDOW (5 * setup->kmig_period)  // post-demotion window: 5 phases
static long long g_demo_age_sum[4][4], g_demo_cnt[4][4], g_remain_cnt[4][4];

static void demo_analysis_init(int nr_va) {
    g_alloc_inst.assign(nr_va, 0);
    g_demo_inst.assign(nr_va, 0);
    g_demo_src.assign(nr_va, -1);
    g_demo_dst.assign(nr_va, -1);
    memset(g_demo_age_sum, 0, sizeof(g_demo_age_sum));
    memset(g_demo_cnt, 0, sizeof(g_demo_cnt));
    memset(g_remain_cnt, 0, sizeof(g_remain_cnt));
}

// --- MigOpt schedule replay ("migopt" technique) -----------------------------
// Replays the placement schedule computed by MigOpt (migsim)
// under the same accounting as the online policies. The schedule lists, per
// migration period, the tier of every page placed so far.
static std::map<long long, std::vector<std::pair<int,int>>> g_sched;   // time -> (va, tier)
static std::map<long long, std::vector<std::pair<int,int>>>::iterator g_sched_it;
static std::vector<signed char> g_target_tier;   // scheduled tier per va

static int load_sched(const char *path, int nr_va) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open schedule %s\n", path); return -1; }
    char tag[4]; long long t; long long va; int tier, extra;
    while (fscanf(f, "%3s %lld %lld %d %d", tag, &t, &va, &tier, &extra) == 5) {
        if (va < 0 || va >= nr_va) continue;
        g_sched[t].push_back({(int)va, tier});
    }
    fclose(f);
    g_sched_it = g_sched.begin();
    g_target_tier.assign(nr_va, -1);
    return 0;
}

// Apply every schedule step with time <= now; count tier changes as migrations.
static void sched_apply(long long now, SimulStruct *sys, SimulSetup *setup) {
    while (g_sched_it != g_sched.end() && g_sched_it->first <= now) {
        for (auto &pv : g_sched_it->second) {
            int va = pv.first, tier = pv.second;
            int cur = g_target_tier[va];
            if (cur >= 0 && cur != tier) {
                sys->mig_info[cur][tier] += 1;
                if (tier > cur) {
                    g_demo_age_sum[cur][tier] += now - g_alloc_inst[va];
                    g_demo_cnt[cur][tier] += 1;
                    g_demo_src[va] = (signed char)cur;
                    g_demo_dst[va] = (signed char)tier;
                    g_demo_inst[va] = now;
                } else {
                    g_demo_src[va] = -1; g_demo_dst[va] = -1;
                }
            }
            g_target_tier[va] = (signed char)tier;
        }
        ++g_sched_it;
    }
}

static int replay_access(int va, SimulStruct *sysstruct, SimulSetup *setup) {
    int tier = g_target_tier[va];
    if (tier < 0) tier = 0;   // touched before its first scheduled placement
    if (sysstruct->mtable[va] == -1) {
        sysstruct->mtable[va] = 1;   // mark as allocated (slots are virtual here)
        sysstruct->alloc_info[tier] += 1;
        g_alloc_inst[va] = setup->inst;
    }
    sysstruct->access_info[tier] += 1;
    if (g_demo_src[va] >= 0 && setup->inst - g_demo_inst[va] <= REMAIN_WINDOW)
        g_remain_cnt[g_demo_src[va]][g_demo_dst[va]] += 1;
    return 0;
}

// Move va from src_tier to dst_tier, consuming one unit of migration budget.
int move_page(int va,
              int src_tier,
              int dst_tier,
              SimulStruct* sys,
              SimulSetup* setup,
              Tier* tiers) {
    int new_slot = tiers[dst_tier].allocate_free_page();
    if (new_slot == -1) {
        printf("Move page fail: no slot for Tier-%d\n", dst_tier);
        return -1;
    }
    tiers[src_tier].remove(va);
    int old_slot = sys->mtable[va];
    tiers[src_tier].reclaim_free_page(old_slot);

    sys->mtable[va] = new_slot;

    sys->mig_info[src_tier][dst_tier] += 1;
    setup->mig_budget -= 1;
    if (dst_tier > src_tier) {   // demotion: record age + start remain-access tracking
        g_demo_age_sum[src_tier][dst_tier] += setup->inst - g_alloc_inst[va];
        g_demo_cnt[src_tier][dst_tier] += 1;
        g_demo_src[va] = (signed char)src_tier;
        g_demo_dst[va] = (signed char)dst_tier;
        g_demo_inst[va] = setup->inst;
    } else {                     // promotion clears the demotion tag
        g_demo_src[va] = -1;
        g_demo_dst[va] = -1;
    }
    return 0;
}

int identify_tier(SimulSetup *setup, int pa){
    if (pa == -1) return -1;
    int cur_num = setup->tier_cap[0];
    for (int i = 0; i < 4; i++) {
        if ((double)pa / (double)cur_num < 1) return i;
        cur_num += setup->tier_cap[i+1];
    }
    return -1;
}

// --- AutoTiering hint-fault history (8-bit, one bit per sampling window) ----
int count_access_bit(int val) {
    val &= 0xFF;
    int count = 0;
    while (val) {
        count += (val & 1);
        val >>= 1;
    }
    return count;
}

int set_access_bit(int val) {
    val &= 0xFF;
    return val | 0x01;
}

int shift_access_bit(int val) {
    val &= 0xFF;
    return (val << 1) & 0xFF;
}

int auto_alloc(int va, SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, AccessBuckets *ab){
    bool allocate = false;
    for (int k = 0; k < 4; k++) {
        int t = sysstruct->alloc_order[k];
        if (tiers[t].free_list.size() == 0){
            continue;
        }

        int slot = tiers[t].allocate_free_page();
        if (slot == -1) {
            printf("Allocation error\n");
            abort();
        }
        sysstruct->mtable[va] = slot;
        lru_touch(tiers[t], va);
        sysstruct->alloc_info[t] += 1;
        sysstruct->access_info[t] += 1;
        sysstruct->reference_bit[va] = 1;
        sysstruct->fault_access_cnt[va] = set_access_bit(sysstruct->fault_access_cnt[va]);
        allocate = true;

        // track hotness buckets for pages resident in the fast tier
        if (t == 0) {
            ab->insert(va, 1);
        }
        return 0;
    }

    if (!allocate) {
        printf("[Error] Allocation Fail\n");
        abort();
    }
    return 0;
}

int mtm_alloc(int va, SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, ExpHistogram *h){
    bool allocate = false;
    for (int k = 0; k < 4; k++) {
        int t = sysstruct->alloc_order[k];
        if (tiers[t].free_list.size() == 0){
            continue;
        }

        int slot = tiers[t].allocate_free_page();
        if (slot == -1) {
            printf("Allocation error\n");
            abort();
        }
        sysstruct->mtable[va] = slot;
        sysstruct->alloc_info[t] += 1;
        sysstruct->access_info[t] += 1;
        sysstruct->reference_bit[va] = 1;
        sysstruct->access_cnt[va] += 1;
        h->insert(va, 1);
        allocate = true;
        return 0;
    }

    if (!allocate) {
        printf("[Error] Allocation Fail\n");
        abort();
    }
    return 0;
}

// AutoTiering reclaim daemon: once per phase, top up a small free pool in T0
// by demoting the LRU-coldest pages straight to the capacity tiers (T2/T3).
int auto_demo(SimulStruct *sys, SimulSetup *setup, Tier *tiers, AccessBuckets *ab){
    const int dsts[2] = {2,3};

    // shift the hint-fault history once per sampling window (3 phases)
    if (setup->inst % ((setup->kmig_period)*3) == 0) {
        for (int i = 0; i < setup->unique_va_num; i++) {
            sys->fault_access_cnt[i] = shift_access_bit(sys->fault_access_cnt[i]);

            if (identify_tier(setup, sys->mtable[i]) == 0) ab->decrement(i);
        }
    }

    if (has_spare_free(tiers[0], setup, 0, setup->auto_reclaim_low)) {
        return 0;
    }

    if (setup->inst % setup->kmig_period != 0) return 0;

    for (int k = 0; k < 2; ++k) {
        int src = 0;
        int dst = dsts[k];

        // free pool the daemon maintains, proportional to the tier size
        size_t needed = (size_t)ceil(setup->tier_cap[0] * 0.02);
        while (tiers[src].free_list.size() < needed && setup->mig_budget > 0) {
            // victims come from the LRU tail
            int victim = tiers[src].oldest();
            if (victim == -1) break;
            if (tiers[dst].free_list.size() <= 0){
                break;
            }
            victim = tiers[src].evict_oldest();
            int ret;
            ret = move_page(victim, src, dst, sys, setup, tiers);
            lru_touch(tiers[dst], victim);
            ab->erase(victim);
        }
    }

    return 0;
}

int auto_access(int va, SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, AccessBuckets *ab) {

    int pa = sysstruct->mtable[va];
    int cur_tier = identify_tier(setup, pa);
    int dst_tier = 0;
    int ret;
    if (sysstruct->reference_bit[va] == 0) {   // hint fault: first touch this phase

        sysstruct->reference_bit[va] = 1;

        sysstruct->fault_access_cnt[va] = set_access_bit(sysstruct->fault_access_cnt[va]);

        if (cur_tier == 0) {
            sysstruct->access_info[cur_tier] += 1;
            lru_touch(tiers[cur_tier], va);
            ab->increment(va);
        }
        else {
            // promotion needs a free T0 slot and remaining migration budget
            if(tiers[dst_tier].free_list.size() == 0 || setup->mig_budget <= 0){
                sysstruct->access_info[cur_tier] += 1;
                lru_touch(tiers[cur_tier], va);
            }
            else {
                int hotness_level = count_access_bit(sysstruct->fault_access_cnt[va]);
                // opportunistic promotion: promote on the hint fault if the
                // page is hotter than T0's coldest resident
                if (hotness_level > ab->min_bucket()){
                    ret = move_page(va, cur_tier, dst_tier, sysstruct, setup, tiers);
                    if (ret == -1) {
                        printf("[AutoTiering] Move page fail\n");
                    }
                    sysstruct->access_info[dst_tier] += 1;
                    lru_touch(tiers[dst_tier], va);
                    ab->insert(va, count_access_bit(sysstruct->fault_access_cnt[va]));
                }
            }
        }
    }
    else {
        sysstruct->access_info[cur_tier] += 1;
        lru_touch(tiers[cur_tier], va);
    }
    return 0;
}

// Rank index -> target tier: the hottest cap[0] pages belong in T0, the next
// cap[1] in T1, and so on.
static inline int target_tier_for_rank(size_t rank, const SimulSetup* setup) {
    size_t c0 = setup->tier_cap[0];
    size_t c1 = c0 + setup->tier_cap[1];
    size_t c2 = c1 + setup->tier_cap[2];
    if (rank < c0) return 0;
    if (rank < c1) return 1;
    if (rank < c2) return 2;
    return 3;
}

// Rank every page by histogram hotness and pick pages residing below their
// target tier, up to half the per-phase quota.
list<pair<int, int>>
scan_mtm_promo_cand(SimulStruct* sysstruct,
                    SimulSetup*  setup,
                    Tier*        tiers,
                    ExpHistogram& h)
{
    list<pair<int, int>> result;
    const int max_pick = max(0, (int)(setup->mig_quota / 2.));

    vector<int> ranked;
    ranked.reserve(setup->unique_va_num);

    for (int bin = h.bin_count() - 1; bin >= 0; --bin) {
        const auto& lst = h.bin_elems(bin);
        for (int va : lst) ranked.push_back(va);
    }

    for (size_t i = 0; i < ranked.size(); ++i) {
        int va = ranked[i];
        int tgt = target_tier_for_rank(i, setup);
        int cur = identify_tier(setup, sysstruct->mtable[va]);
        if (cur == -1) {
            printf("[Error] Current tier of promo candidate is -1 (VA: %d)\n", va);
        }
        if (cur > tgt) {
            result.emplace_back(va, tgt);
            if ((int)result.size() >= max_pick) break;
        }
    }
    return result;
}

// For every tier that lacks room for its scheduled promotions, pick the
// coldest resident pages as demotion victims; the shortage cascades one tier
// down at a time.
list<pair<int, int>> scan_mtm_demotion_cascade(
    SimulStruct* sys,
    SimulSetup* setup,
    Tier* tiers,
    list<pair<int, int>>& promo_cand_list,
    ExpHistogram& h
) {
    constexpr int MAX_TIER = 4;
    int promo_demand[MAX_TIER] = {0};
    int tier_free_space[MAX_TIER] = {0};
    int demotion_needed[MAX_TIER] = {0};

    for (auto& p : promo_cand_list) {
        int target_tier = p.second;
        promo_demand[target_tier]++;
    }

    for (int i = 0; i < MAX_TIER; ++i)
        tier_free_space[i] = tiers[i].free_list.size() - 1;

    for (int i = 0; i < MAX_TIER - 1; ++i) {
        int shortage = promo_demand[i] - tier_free_space[i];
        if (shortage > 0) {
            demotion_needed[i] = shortage;
            promo_demand[i + 1] += shortage;
        }
    }

    list<pair<int, int>> result;

    for (int tier = MAX_TIER - 2; tier >= 0; --tier) {
        int need = demotion_needed[tier];
        if (need <= 0) continue;

        for (int bin = 0; bin < h.bin_count(); ++bin) {
            const auto& entries = h.get_bin(bin);
            for (int va : entries) {
                if (identify_tier(setup, sys->mtable[va]) != tier)
                    continue;

                result.emplace_back(va, tier);
                if (--need == 0) break;
            }
            if (need == 0) break;
        }
    }

    return result;
}

// Cooling: decay the histogram one bin and halve every access counter.
void apply_cooling(ExpHistogram* h, SimulStruct* sysstruct, SimulSetup* setup) {
    h->cooling();

    for (int i = 0; i < setup->unique_va_num; ++i) {
        sysstruct->access_cnt[i] /= 2;
    }
}

int mtm_demo(list<pair<int, int>> &demo_cand_list, SimulStruct *sys, SimulSetup *setup, Tier *tiers) {
    for (const auto&[va, cur_tier] : demo_cand_list) {
        int dst_tier = cur_tier + 1;
        if (dst_tier > 3) {
            printf("[Error] demotion below the last tier\n");
            abort();
        }
        int ret;
        ret = move_page(va, cur_tier, dst_tier, sys, setup, tiers);
        // cascading demotions are collateral traffic; only the promotions
        // themselves consume the migration quota
        if (ret == 0) setup->mig_budget += 1;
    }
    return 0;
}

int mtm_promo(list<pair<int, int>> &promo_cand_list, SimulStruct *sys, SimulSetup *setup, Tier *tiers){
    for (const auto& [va, dst_tier] : promo_cand_list) {
        int cur_tier = identify_tier(setup, sys->mtable[va]);
        if (dst_tier < 0) {
            printf("[Error] dst tier is %d\n", dst_tier);
            abort();
        }
        if (setup->mig_budget > 0){
            move_page(va, cur_tier, dst_tier, sys, setup, tiers);
        }
    }
    return 0;
}

int mtm_access(int va, SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, ExpHistogram *h) {
    int pa = sysstruct->mtable[va];
    int cur_tier = identify_tier(setup, pa);
    list<pair<int,int>> promo_cand_list;
    list<pair<int,int>> demo_cand_list;

    // PMU sampling interval, scaled with the phase length (1000 samples per
    // migration phase)
    int sample_interval = setup->kmig_period / 1000 > 0 ? setup->kmig_period / 1000 : 1;
    if (setup->inst % sample_interval == 0) {
        sysstruct->access_cnt[va] += 1;
        if (sysstruct->access_cnt[va] > 1024) sysstruct->access_cnt[va] = 1024;
    }
    sysstruct->access_info[cur_tier] += 1;
    h->check_and_update(va, sysstruct->access_cnt[va]);
    if (setup->inst % setup->kmig_period == 0) {
        promo_cand_list = scan_mtm_promo_cand(sysstruct, setup, tiers, *h);
        demo_cand_list = scan_mtm_demotion_cascade(sysstruct, setup, tiers, promo_cand_list, *h);
        mtm_demo(demo_cand_list, sysstruct, setup, tiers);
        mtm_promo(promo_cand_list, sysstruct, setup, tiers);
    }

    if (setup->inst % setup->kmig_period == 0) apply_cooling(h, sysstruct, setup);
    return 0;
}

int do_sim(char *tech, FILE *file, SimulStruct *sysstruct, SimulSetup *setup, Tier *tiers, AccessBuckets *ab, ExpHistogram *h) {
    int pa;
    int cur_tier;
    char op;
    int va;
    while (fscanf(file, "%c %d\n", &op, &va) == 2) {
        if (strcmp(tech, "migopt") == 0) {
            sched_apply(setup->inst, sysstruct, setup);
            replay_access(va, sysstruct, setup);
            setup->inst++;
            continue;
        }

        pa = sysstruct->mtable[va];
        cur_tier = identify_tier(setup, pa);
        if (cur_tier == -1) {   // first touch: allocate
            if(strcmp(tech, "auto") == 0) auto_alloc(va, sysstruct, setup, tiers, ab);
            else if(strcmp(tech, "mtm") == 0) mtm_alloc(va, sysstruct, setup, tiers, h);
            else {
                printf("No technique: %s\n", tech);
                abort();
            }
            g_alloc_inst[va] = setup->inst;
        }
        else {
            if(strcmp(tech, "auto") == 0) auto_access(va, sysstruct, setup, tiers, ab);
            else if(strcmp(tech, "mtm") == 0) mtm_access(va, sysstruct, setup, tiers, h);
            if (g_demo_src[va] >= 0 && setup->inst - g_demo_inst[va] <= REMAIN_WINDOW)
                g_remain_cnt[g_demo_src[va]][g_demo_dst[va]] += 1;
        }

        if (strcmp(tech, "auto") == 0) auto_demo(sysstruct, setup, tiers, ab);
        if (setup->inst % setup->kmig_period == 0) {   // phase boundary
            setup->mig_budget = setup->mig_quota;
            memset(sysstruct->reference_bit, 0, sizeof(int)*setup->unique_va_num);
        }
        setup->inst++;
    }

    return 0;
}

// Per src->dst demotion path: count, average page age at demotion (in
// instructions since allocation), and average accesses received while demoted.
void print_demotion_analysis() {
    cout << "\n[Demotion Analysis] (src -> dst: count / avg_age_inst / avg_remain_access)\n";
    for (int src = 0; src < 3; ++src) {
        cout << "src " << src;
        for (int dst = src + 1; dst < 4; ++dst) {
            long long c = g_demo_cnt[src][dst];
            double age = c ? (double)g_demo_age_sum[src][dst] / c : 0.;
            double rem = c ? (double)g_remain_cnt[src][dst] / c : 0.;
            cout << "  ->" << dst << ": " << c << " / "
                 << (long long)age << " / " << fixed << setprecision(1) << rem;
            cout.unsetf(ios::fixed);
        }
        cout << '\n';
    }
}

void print_access_stats(const SimulStruct *sys,
                        const SimulSetup  *setup) {
    long long total_time = 0;

    cout << "\n[Access Stats]\n"
              << "Tier | Accesses | Latency(ns) | Time(ns)\n"
              << "----------------------------------------\n";

    for (int t = 0; t < 4; ++t) {
        long long acc   = sys->access_info[t];
        long long time  = acc * setup->tier_load_lat[t];
        total_time     += time;

        cout << setw(4) << t << " | "
                  << setw(8) << acc  << " | "
                  << setw(11) << setup->tier_load_lat[t] << " | "
                  << setw(9) << time << '\n';
    }
    cout << "----------------------------------------\n"
              << "Total access time = " << total_time << " ns\n";
}

void print_migration_stats(const SimulStruct *sys,
                           const SimulSetup  *setup) {
    cout << "\n[Migration Matrix] (count)\n      →  0      1      2      3\n";

    long long total_mig_time = 0;
    int total_mig = 0;
    int total_promo = 0;
    int total_demo = 0;
    for (int src = 0; src < 4; ++src) {
        cout << "src " << src;
        for (int dst = 0; dst < 4; ++dst) {
            cout << setw(7) << sys->mig_info[src][dst];
            // one migration = read(src) + write(dst)
            long long one_time = setup->tier_4KB_R_lat[src] +
                                 setup->tier_4KB_W_lat[dst];
            total_mig_time += static_cast<long long>(sys->mig_info[src][dst])
                              * one_time;
            total_mig += sys->mig_info[src][dst];
            if (src < dst) total_demo += sys->mig_info[src][dst];
            else total_promo += sys->mig_info[src][dst];
        }
        cout << '\n';
    }
    printf("Total Migration count: %d (Promo: %d/Demo: %d)\n", total_mig, total_promo, total_demo);
    cout << "Total migration time = " << total_mig_time << " ns\n";
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s <auto|mtm> <trace_file>\n"
                        "       %s migopt <trace_file> <schedule_file>\n",
                argv[0], argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "migopt") == 0 && argc != 4) {
        fprintf(stderr, "migopt replay needs a schedule file\n");
        return 1;
    }
    if (strcmp(argv[1], "auto") != 0 && strcmp(argv[1], "mtm") != 0 &&
        strcmp(argv[1], "migopt") != 0) {
        fprintf(stderr, "unknown technique: %s\n", argv[1]);
        return 1;
    }

    char *tech = argv[1];
    FILE *input = fopen(argv[2], "r");
    if (!input) {
        perror("fopen");
        return 1;
    }

    Tier tiers[4];
    SimulStruct sysstruct;
    SimulSetup setup;
    AccessBuckets ab;
    ExpHistogram h;
    set_system_param(&setup, input);
    build_simul_system(&sysstruct, &setup, tiers, tech);
    demo_analysis_init(setup.unique_va_num);
    if (strcmp(tech, "migopt") == 0 && load_sched(argv[3], setup.unique_va_num) != 0)
        return 1;
    do_sim(tech, input, &sysstruct, &setup, tiers, &ab, &h);

    print_access_stats(&sysstruct, &setup);
    print_migration_stats(&sysstruct, &setup);
    print_demotion_analysis();
}
