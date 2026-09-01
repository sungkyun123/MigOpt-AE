#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <limits.h>
#include <limits>
#include <deque>
#include <queue>
#include <tuple>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <ctime>
#include <unordered_set>
#include <string>
#include <list>
#include "sim.h"
using namespace std;

#define NR_COMP 1

#define COOLING_PERIOD 3

#define NUM_META_L 9
#define BASE_OFFSET NUM_META_L
#define L_DEMO 1
#define L_DEMO_BAR 2
#define L_PROMO 3
#define L_PROMO_BAR 4
#define L_ALLOC 5
#define L_END 6
#define L_AGGR 7
#define L_AGGR_BAR 8


#define N_NOP -1
#define N_META 0
#define N_DEMO 1
#define N_DEMO_BAR 2
#define N_PROMO 3
#define N_PROMO_BAR 4
#define N_ALLOC 5
#define N_END 6
#define N_AGGR 7
#define N_AGGR_BAR 8
#define N_REG NUM_META_L


#define E_EVICTION 0
#define E_CACHING 1
#define E_LOAD 2
#define E_STORE 3
#define E_SINK 4
#define E_REV 5

#define E_AGGR 6
#define E_BAR 7
#define E_HIT 8
#define E_DEMO 9
#define E_PROMO 10
#define E_MIG 11
#define E_END 12
#define E_DEMO_NEXT 13
#define E_PROMO_ALLOC 14
#define E_CHOCK 15
#define E_ALLOC 16

#define NR_ITEM_STAT 9
#define S_CNT_GLO 0
#define S_CNT_LOC 1
#define S_CNT_ACC 2
#define S_REU_GLO 3
#define S_REU_LOC 4
#define S_REU_ACC 5
#define S_INT_GLO 6
#define S_INT_LOC 7
#define S_INT ACC 8

#define I_TYPE_TOTAL 0
#define I_TYPE_ALLOC 1
#define I_TYPE_REMAIN 2
#define I_TYPE_PROMO 3
#define I_TYPE_DEMO 4
#define NR_I 5

#define T_TOTAL 0
#define T_ALLOC 1
#define T_REMAIN 2
#define T_PROMO 3
#define T_DEMO 4
#define NR_T 5

#define FAULT_OVERHEAD 2000

#define MIG_STAT_VAL 0

struct mig_stat {
	int layer;
	int nr_new_alloc;
	int type;
	int cnt[3];
	int reuse[3];
	int inter_ref[3];
};

struct analysis_stat {
	uint64_t nr_accesses;
	uint64_t nr_loads;
	uint64_t nr_stores;
	uint64_t nr_promo;
	uint64_t nr_demo;
};

struct anode {
	int index_;
	int layer;
	uint64_t addr;
	enum trace_type type;
	int node_type;
	int reuse_dist;
	int inter_ref;
	int alloc_time;
	int alloc_dist;
	int ref_cnt;
	int access_time;
	int period_idx;
	int prev_layer;
};

struct page_anal {
	uint64_t addr;
	int cur_layer;
	int prev_layer;
	int expected_layer;
	int type;
	pair<int,int> next_action;
	int inter_ref;
	int next_access_dist;
	int freq;
	vector<int> past_freq_window;
	vector<int> future_freq_window;
	int future_freq_inclusive;
	int future_freq_exclusive;
	int future_freq_same_tier;
	vector<int> future_freq_same_tier_window;
	int last_access_time;
	int last_access_time_dist;
	int cur_freq;
	int hotness;
	int alloc_time;
	int alloc_dist;
};

struct page_stat_def {
    float mean;
    int   min;
    int   max;
    float percentile_5;
    float percentile_95;
    float variance;
    float stddev;
    float median;
    float q1;
    float q3;
    float iqr;         // IQR(Q3 - Q1)
    float skewness;
    float kurtosis;
    size_t count;
    float sum;
};

struct page_stat {
	int cnt;
	vector<int> last_access_time_dists;
	vector<int> next_access_dists;
	vector<int> freqs;
	vector<int> past_freqs_window;
	vector<vector<int>> future_freqs_window;
	vector<int> future_freqs_inclusive;
	vector<int> future_freqs_exclusive;
	vector<int> future_freqs_same_tier;
	vector<vector<int>> future_freqs_same_tier_window;
	vector<int> cur_freqs;
	vector<int> hotnesses;
	vector<int> alloc_dists;

	struct page_stat_def last_access_time_dist_stat;
	struct page_stat_def next_access_dist_stat;
	struct page_stat_def freq_stat;
	vector<struct page_stat_def> future_freq_window_stat;
	struct page_stat_def future_freq_inclusive_stat;
	struct page_stat_def future_freq_exclusive_stat;
	struct page_stat_def future_freq_same_tier_stat;
	vector<struct page_stat_def> future_freq_same_tier_window_stat;
	struct page_stat_def cur_freq_stat;
	struct page_stat_def hotness_stat;
	struct page_stat_def alloc_dist_stat;

	int type;

	/*
	float avg_last_access_time_dist;
	float avg_next_access_dist;
	float avg_freq;
	float avg_future_freq;
	float avg_cur_freq;
	float avg_hotness;
	float avg_alloc_dist;

	float max_last_access_time_dist;
	float max_next_access_dist;
	float max_freq;
	float max_future_freq;
	float max_cur_freq;
	float max_hotness;
	float max_alloc_dist;

	float min_last_access_time_dist;
	float min_next_access_dist;
	float min_freq;
	float min_future_freq;
	float min_cur_freq;
	float min_hotness;
	float min_alloc_dist;
	*/
};

struct analysis {
	struct anode *nodes;
	int64_t base_cost;
	int base_layer;
	int period;
	int mig_traffic;
	unordered_map<uint64_t, int> prev_accesses;
	unordered_map<uint64_t, vector<int>> total_accesses;
	unordered_map<int, vector<int>> total_inter_ref;
	int nr_traces;
	int nr_tiers;
	int nr_nodes;
	int cur;
	int nr_cache_pages[MAX_NR_TIERS];
	int lat_loads[MAX_NR_TIERS];
	int lat_stores[MAX_NR_TIERS];
	int lat_4KB_reads[MAX_NR_TIERS];
	int lat_4KB_writes[MAX_NR_TIERS];
	struct analysis_stat stats[MAX_NR_TIERS];
	struct analysis_stat g_stat;
	int nr_items;
	int nr_period;
	int bar_ratio;
	char *alloc_file;

	// for the cache schedule
	// period, layer, addr, type
	vector<vector<unordered_map<uint64_t,vector<struct anode *>>>> cache_sched;

	// period, layer, T type, addr, page anal
	vector<vector<vector<unordered_map<uint64_t, struct page_anal *>>>> total_page_anal;

	// period, layer, T type, page stat
	vector<vector<vector<struct page_stat>>> total_page_stat;

	// period, mig_path, page_stat
	vector<map<pair<int,int>, struct page_stat>> page_stat_per_path;

	// layer, type (-1: unknown, 0: ALLOC, 1: PROMO, 2: HIT, 3: DEMO), local accesses, initial alloc time, local alloc time, reuse distance, inter ref
	unordered_map<uint64_t,vector<struct mig_stat>> item_sched;
	unordered_map<uint64_t, vector<struct anode*>> access_per_addr;

	unordered_map<uint64_t,vector<int>> demo_addr;
	unordered_map<uint64_t,vector<int>> promo_addr;

	unordered_set<uint64_t> demo_addr_set;
	unordered_set<uint64_t> promo_addr_set;

	vector<map<pair<int,int>,unordered_set<uint64_t>>> demo_addr_by_period;
	vector<map<pair<int,int>,unordered_set<uint64_t>>> promo_addr_by_period;
	vector<map<int,unordered_set<uint64_t>>> alloc_addr_by_period;
	vector<map<int,unordered_set<uint64_t>>> remain_addr_by_period;

	unordered_set<uint64_t> g_first;
	vector<int> reuse_dist;
	std::list<uint64_t> lru;

};

void init_analysis(struct sim_cfg &scfg);
void destroy_analysis(void);
void analysis_add_trace(struct trace_req &t);
void do_analysis(const char *sched_file, bool detailed);
void print_analysis();


#endif
