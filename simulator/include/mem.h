#ifndef __MEM_H__
#define __MEM_H__

#include <unordered_map>
#include <pthread.h>
#include <stdio.h>
#include <vector>
#include "cxl_tm.h"
#include "list.h"

struct memory_stat {
	uint64_t nr_accesses;
	uint64_t nr_loads;
	uint64_t nr_stores;
	uint64_t nr_promo;
	uint64_t nr_demo;
};

struct frame {
	uint64_t addr;
	bool present;
	int accessed;
	pthread_rwlock_t rwlock;
	int tier;
	int state;
	li_node *lnode;
	list *cur_list;
	struct memory_stat mstat;
	void *mp;
	// tier
	// promotion
};

struct page {
	uint64_t addr;
	int rank;
	int static_tier;
	int tier;
	struct memory_stat mstat;
};

struct mapping {
	struct page *page;
	struct frame *frame;
};

struct mem_req {
	enum req_type type;
	unsigned long long vaddr;
	unsigned long long paddr;
	struct mapping *mp;
	int len;
	uint64_t time;
};

struct pa_inf {
	pthread_mutex_t mutex;
	struct page *pages;
	struct frame *frames;
	std::unordered_map<uint64_t, struct mapping *> *v2p_map;
	int num_frames;
	int cur_frames;
	int nr_tiers;
	int policy;
	int cur_tier;
	int *ratio;
	int ratio_sum;
	int alloc_id;
};

static inline void print_frame (struct frame *f) {
	printf("frame %lu: %lu %lu %lu\n", f->addr, f->mstat.nr_accesses, f->mstat.nr_loads, f->mstat.nr_stores);
}

static inline void print_page (struct frame *p) {
	printf("page %lu: %lu %lu %lu\n", p->addr, p->mstat.nr_accesses, p->mstat.nr_loads, p->mstat.nr_stores);
}

void init_pa_inf (uint64_t nr_pages, int *ratio, int nr_tiers, int policy, int *promo_target, int *demo_target, int nr_access_bits, int *nr_cache_pages, int *lat_loads, int *lat_stores, int *lat_4KB_reads, int *lat_4KB_writes, int alloc_id);
void destroy_pa_inf ();
int make_mreq (struct mem_req &mreq, enum req_type type, unsigned long long addr, int len, int tier, uint64_t time);
void proc_mreq (struct mem_req &mreq);
struct pa_inf *get_pinf();
void print_pa_inf ();
struct mapping *v2p (uint64_t va);
void update_v2p (struct frame *old_f, struct frame *new_f);
struct mapping *mcmf_v2p (uint64_t va, int layer, std::vector<std::vector<uint64_t>> &res);


#endif
