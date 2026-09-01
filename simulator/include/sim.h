#ifndef __SIM_H__
#define __SIM_H__

#include <stdint.h>

#define PAGE_SIZE 4096
#define FRAME_SIZE 4096

#define TRACE_SAMPLE 10000LL

#define HOT 0 
#define WARM 1
#define COLD 2

#define MPOL_BIND 0
#define MPOL_PREFERRED 1
#define MPOL_INTERLEAVE 2
#define MPOL_BELADY 3
#define MPOL_STATIC_OFFLINE 4
#define MPOL_REV_PREFERRED 5
#define MPOL_BIND_LRU 6
#define MPOL_BIND_FIFO 7
#define MPOL_MCMF 8

#define MON_POL_NOP -1
#define MON_POL_SCAN 0
#define MON_POL_SAMPLE 1

#define MIG_POL_NOP -1
#define MIG_POL_WF 0
#define MIG_POL_RWF 1

#define MCMF_CHOPT 0
#define MCMF_MIGOPT 1

#define MAX_NR_TIERS 4

struct sim_cfg {
	char trace_file[200]; // input trace file
	char sampled_file[200]; // sampled trace file
	char sched_file[200]; // output schedule file
	char analysis_input_file[200]; // output schedule file

	uint64_t nr_org_pages; // number of original pages
	uint64_t nr_org_traces; // number of original traces
	int trace_sampling_ratio; // 10000: 100%
	uint64_t nr_sampled_pages; // number of sampled pages
	uint64_t nr_sampled_traces; // number of sampled traces

	int nr_tiers; // number of tiers
	int tier_cap_scale; // total cap = nr_sampled_pages * tier_cap_scale / 100
	int tier_cap_ratio[MAX_NR_TIERS]; // ratio of each tier
	int total_cap; // total capacity
	int tier_cap[MAX_NR_TIERS]; // cap of each tier

	int tier_lat_loads[MAX_NR_TIERS]; // latency of loads
	int tier_lat_stores[MAX_NR_TIERS]; // latency of stores
	int tier_lat_4KB_reads[MAX_NR_TIERS]; // latency of 4KB reads
	int tier_lat_4KB_writes[MAX_NR_TIERS]; // latency of 4KB writes

	int mig_period; // migration period
	int mig_traffic; // migration traffic
	int mig_overhead; // 10000: 100%

	int do_an; // 0: do not AutoNUMA, 1: do Balanced AutoNUMA, 2: do Tiered AutoNUMA, 3: do No Migration
	int do_at; // 0: do not AutoTiering, 1: do AutoTiering
	int do_mtm; // 0: do not MTM, 1: do default MTM, 2: do prioritized MTM
	int do_migopt; // 0: do not MigOpt, 1: do MigOpt
	int do_analysis; // 0: do not analysis, 1: do analysis


	/*
	int mcmf_type;
	int mcmf;
	int mcmf_period;
	int mcmf_mig_traffic;
	*/
};


#define NR_REQ_TYPE 6
enum trace_type {
	TOTAL = 0,
	LOAD,
	STORE,
	ALLOC,
	FREE,
	OTHERS,
};

struct trace_req {
	uint64_t addr;
	enum trace_type type;
	int tier;
};

struct sim_stat {
	uint64_t nr_org_traces[NR_REQ_TYPE];
	uint64_t nr_sampled_traces[NR_REQ_TYPE];
};

#endif