#ifndef __TIER_H__
#define __TIER_H__

#include <stdint.h>
#include <stdio.h>
#include <queue>
#include <vector>
#include "list.h"
#include "mem.h"



struct tier_stat {
	int nr_promo;
	int nr_demo;
	int nr_alloc_pages;
	uint64_t nr_promo_pages;
	uint64_t nr_demo_pages;
	uint64_t nr_accesses;
	uint64_t nr_loads;
	uint64_t nr_stores;
	uint64_t cur_nr_pages;
};

struct tier {
	uint64_t max_nr_pages;
	int arm;
	int nr_tiers;
	int promo_target;
	int demo_target;
	int demotion_high_wm;
	int demotion_low_wm;
	int promotion_high_wm;
	int promotion_low_wm;
	int id;
	bool need_demotion;
	bool need_promotion;
	list *promo_hot_list;
	list *demo_warm_list;
	list *demo_cold_list;
	list *lru;
	struct tier_stat tstat;
	std::queue<struct frame *> *frame_q;
	struct frame *frames;
	int max_accesses;

	int lat_load;
	int lat_store;
	int lat_promo;
	int lat_demo;
	int lat_alloc;
};


void init_tiers (uint64_t nr_pages, int *ratio, int nr_tiers, int policy, struct frame *frames, int *promo_target, int *demo_target, int nr_access_bits, int *nr_cache_pages, int *lat_loads, int *lat_stores, int *lat_4KB_reads, int *lat_4KB_writes);
void destory_tiers ();
struct frame *get_tier_frame(int id);
void put_tier_frame(int id, struct frame *frame);
void proc_tier(struct mem_req  &mreq);

bool do_eviction_if_needed (int id, int mpol);
void proc_tier_demand(struct mem_req  &mreq, int policy);
bool is_available(int id);

struct tier *get_tiers();
void update_tier_alloc(int id, int cnt);
double calc_alloc_latency();
double calc_load_latency();
double calc_store_latency();
double calc_promo_latency();
double calc_demo_latency();
double calc_total_latency();
std::vector<std::vector<uint64_t>> get_migration_pages();

#endif
