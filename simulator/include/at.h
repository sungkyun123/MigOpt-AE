#ifndef __AT_H__
#define __AT_H__

#include <cstdint>
#include <map>
#include <list>
#include <string>
#include "sim.h"
using namespace std;

struct at_page {
	uint64_t addr;
	int freq;
	int tier;
	std::list<struct at_page *>::iterator g_iter;
	std::list<struct at_page *>::iterator t_iter;
};

struct at_tier {
	int cap;
	int size;
	std::list<struct at_page *> *lru_list;
};

struct at_perf {
	int64_t lat_acc;
	int64_t lat_mig;
	int64_t lat_alc;
};

struct at {
	struct at_tier tiers[MAX_NR_TIERS];
	int nr_alloc[MAX_NR_TIERS];
	int nr_loads[MAX_NR_TIERS];
	int nr_stores[MAX_NR_TIERS];
	int nr_accesses[MAX_NR_TIERS];
	int nr_mig[MAX_NR_TIERS][MAX_NR_TIERS];
	int alloc_order[MAX_NR_TIERS];
	struct at_perf perf;
	int nr_tiers;
	int mig_traffic;
	int mig_period;
	int mode;
	int tier_lat_loads[MAX_NR_TIERS];
	int tier_lat_stores[MAX_NR_TIERS];
	int tier_lat_4KB_reads[MAX_NR_TIERS];
	int tier_lat_4KB_writes[MAX_NR_TIERS];
	char *sched_file;


	map<uint64_t, struct at_page *> pt;
	std::list<struct at_page *> *lru_list;
};

void at_add_trace(struct trace_req &t);
void init_at(struct sim_cfg &scfg);
string do_at();
void destroy_at();

#endif