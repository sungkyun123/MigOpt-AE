#ifndef __MTM_H__
#define __MTM_H__

#include <cstdint>
#include <map>
#include <string>
#include "sim.h"
using namespace std;

#define M_DEFAULT 1
#define M_ALL 2

struct mtm_page {
	uint64_t addr;
	int freq;
	int tier;
	int target;
};

struct mtm_tier {
	int cap;
	int size;
};

struct mtm_perf {
	int64_t lat_acc;
	int64_t lat_mig;
	int64_t lat_alc;
};

struct mtm {
	struct mtm_tier tiers[MAX_NR_TIERS];
	int nr_alloc[MAX_NR_TIERS];
	int nr_loads[MAX_NR_TIERS];
	int nr_stores[MAX_NR_TIERS];
	int nr_accesses[MAX_NR_TIERS];
	int nr_mig[MAX_NR_TIERS][MAX_NR_TIERS];
	int alloc_order[MAX_NR_TIERS];
	struct mtm_perf perf;
	int nr_tiers;
	int mig_traffic;
	int mig_period;
	int mode;
	int tier_lat_loads[MAX_NR_TIERS];
	int tier_lat_stores[MAX_NR_TIERS];
	int tier_lat_4KB_reads[MAX_NR_TIERS];
	int tier_lat_4KB_writes[MAX_NR_TIERS];
	char *sched_file;

	map<int, map<uint64_t, struct mtm_page *>> hist;
	map<uint64_t, struct mtm_page *> pt;

};

void mtm_add_trace(struct trace_req &t); 
void init_mtm(struct sim_cfg &scfg);
string do_mtm();
void destroy_mtm();

#endif