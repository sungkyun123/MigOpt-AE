#include <stdio.h>
#include <algorithm>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <iostream>
#include <fstream>
#include <map>
#include "migopt.h"

using namespace std;

static struct migopt my_migopt;

static vector<struct migopt_trace_req> traces;

static struct edge init_edge(int start, int next, int cap, int cost, int flow, enum edge_type type, int associated_tier = INVALID) {
	struct edge e = {start, next, cap, cost, flow, type, associated_tier};
	return e;
}

static void add_edge(int idx, struct edge e) {
	struct node *n = my_migopt.nodes + idx;

	if (e.start < 0 || e.start > my_migopt.nr_nodes) abort();
	if (e.next < 0 || e.next > my_migopt.nr_nodes) abort();

	n->adj->push_back(e);
}

static void init_node(int idx, struct trace_req t, enum node_type type) {
	struct node *n = my_migopt.nodes + idx;

	if (n->ntype != NODE_NULL) {
		printf("Node[%d] already initialized\n", idx);
		abort();
	}

	n->req = t;
	n->ntype = type;

	n->adj = new vector<struct edge>();
}

static void destory_node (struct node *n) {
	if (n->ntype == NODE_NULL)
		return;
	if (n->adj != NULL) { 
		delete(n->adj);
	}
}

static inline int get_layer_from_tier(int tier) {
	// Convert tier to layer index
	// LAYER_ACCESS start from NUM_META_LAYERS to NUM_META_LAYERS + nr_tiers - 1
	// NUM_META_LAYERS is the number of meta layers
	// reverse order: example of nr_tiers = 4
	// tier 0 (top)			: NUM_META_LAYERS + nr_tiers (4) - 2 = 11
	// tier 1 				: NUM_META_LAYERS + nr_tiers (4) - 3 = 10
	// tier 2 				: NUM_META_LAYERS + nr_tiers (4) - 4 = 9
	if (tier < 0 || tier >= my_migopt.nr_tiers - 1) {
		abort();
	}

	return NUM_META_LAYERS + my_migopt.nr_tiers - (tier + 2); // reverse order
}

int get_nidx (int layer, int idx, int tier = INVALID) {
	if (layer == LAYER_ACCESS)
		return get_layer_from_tier(tier) * (my_migopt.nr_traces + 1) + idx;
	return layer * (my_migopt.nr_traces + 1) + idx;
}

int get_tier_from_nidx(int nidx) {
    int nr_traces = my_migopt.nr_traces;
    int nr_tiers = my_migopt.nr_tiers;
    int layer = nidx / (nr_traces + 1);
    
    if (layer < NUM_META_LAYERS || layer > NUM_META_LAYERS + nr_tiers - 2) {
        abort();
    }
    
    int tier = (NUM_META_LAYERS + nr_tiers) - layer - 2;
    
    if (tier < 0 || tier >= nr_tiers - 1) {
        abort();
    }
    
    return tier;
}


int get_nr_nodes(int nr_traces, int nr_tiers) {
	// [blank] --- 	top tier nodes	 	--- [blank]
	// [blank] ---       ... 			---	[blank]
	// [blank] --- 	bottom tier nodes 	--- [blank]
	// ...
	// L0 : [source] [choke] [sink]
	return (nr_traces + 1) * (NUM_META_LAYERS + nr_tiers - 1);
}

static inline int get_nr_period() {
	// Calculate the number of periods based on the number of traces and the migration period
	if (my_migopt.mig_period <= 0) {
		printf("Migration period must be greater than 0\n");
		abort();
	}
	if (my_migopt.nr_traces <= 0) {
		printf("Number of traces must be greater than 0\n");
		abort();
	}

	return (my_migopt.nr_traces - 1) / my_migopt.mig_period + 1;
}

// add source, cap choke, and sink nodes
static void add_default_meta_nodes_edges() {
	struct edge e;

	init_node(get_nidx(LAYER_DEF_META, my_migopt.source_nidx), INVALID_REQ, NODE_SOURCE);
	init_node(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), INVALID_REQ, NODE_CAP_CHOKE);
	init_node(get_nidx(LAYER_DEF_META, my_migopt.sink_nidx), INVALID_REQ, NODE_SINK);

	// IMPORTANT: the capacity choke edge should be added first
	// add the capacity choke edge
	e = init_edge(get_nidx(LAYER_DEF_META, my_migopt.source_nidx), get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), INVALID, 0, 0, EDGE_CAP_CHOKE);
	add_edge(get_nidx(LAYER_DEF_META, my_migopt.source_nidx), e);

	// add the sink edge
	e = init_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), get_nidx(LAYER_DEF_META, my_migopt.sink_nidx), INT_MAX, 0, 0, EDGE_SINK);
	add_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), e);
}

static inline int get_period(int idx) {
	return (idx - 1) / my_migopt.mig_period;
}

static inline int get_period_idx(int idx) {
	return get_period(idx) * my_migopt.mig_period;
}

void init_migopt(struct sim_cfg &scfg) {
	my_migopt.nr_traces = scfg.nr_sampled_traces;
	my_migopt.nr_tiers = scfg.nr_tiers;
	my_migopt.nr_nodes = get_nr_nodes(my_migopt.nr_traces, scfg.nr_tiers);
	my_migopt.mode = scfg.do_migopt;
	my_migopt.sched_file = scfg.sched_file;

	my_migopt.mig_period = scfg.mig_period;
	my_migopt.mig_traffic = scfg.mig_traffic == -1 ? INT_MAX : scfg.mig_traffic;

	for (int i = 0; i < my_migopt.nr_tiers; i++) { // store reverse order
		my_migopt.tier_cap[i] = scfg.tier_cap[i];
		my_migopt.tier_lat_loads[i] = scfg.tier_lat_loads[i];
		my_migopt.tier_lat_stores[i] = scfg.tier_lat_stores[i];
		my_migopt.tier_lat_4KB_reads[i] = scfg.tier_lat_4KB_reads[i];
		my_migopt.tier_lat_4KB_writes[i] = scfg.tier_lat_4KB_writes[i];
	}

	my_migopt.source_nidx = 0;
	my_migopt.cap_choke_nidx = 1;
	my_migopt.sink_nidx = my_migopt.nr_traces;
	my_migopt.bottom_tier = my_migopt.nr_tiers - 1;

	my_migopt.nodes = new struct node[my_migopt.nr_nodes];
	memset(my_migopt.nodes, 0, sizeof(struct node) * my_migopt.nr_nodes);
	for (int i = 0; i < my_migopt.nr_nodes; i++) {
		(my_migopt.nodes + i)->ntype = NODE_NULL;
	}

	traces.push_back({INVALID_REQ, false, false, false, false, INVALID}); // dummy trace for index 0
}

void destroy_migopt () {
	for (int i = 0; i < my_migopt.nr_nodes; i++) {
		destory_node(my_migopt.nodes + i);
	}
	delete [] my_migopt.nodes;
}

static inline bool is_idx_on_mig_period(int idx) {
	return (idx % my_migopt.mig_period) == 0;
}

static inline bool is_diff_mig_period(int idx1, int idx2) {
	return ((idx1-1) / my_migopt.mig_period) != ((idx2-1) / my_migopt.mig_period);
}

static unordered_map<uint64_t, int> prev_accesses;
int get_prev_access_idx(uint64_t addr) {
	int ret = INVALID;
	auto it = prev_accesses.find(addr);
	if (it != prev_accesses.end()) {
		ret = it->second;
	}
	return ret;
}

void set_prev_access_idx(uint64_t addr, int idx) {
	prev_accesses[addr] = idx;
}

void migopt_add_trace(struct trace_req &t) {
	static int cur_idx = 1;

	// 1-indexed

	if (cur_idx > my_migopt.nr_traces) {
		printf("Too many traces, current: %d, max: %d\n", cur_idx, my_migopt.nr_traces);
		abort();
	}

	struct migopt_trace_req mt = {t, true, true, true, true, INVALID, INVALID};

	int prev_idx = get_prev_access_idx(t.addr);
	struct migopt_trace_req *prev_mt;
	if (prev_idx == INVALID) {
		// there is no previous access, so we can just set the current access as the first access
		goto out;
	}

	if (prev_idx >= cur_idx) {
		printf("Previous access index %d is greater than or equal to current index %d\n", prev_idx, cur_idx);
		abort();
	}

	// thers is a previous access, so we need to update the previous access
	prev_mt = &traces[prev_idx];
	prev_mt->next_idx = cur_idx; // set the next index of the previous access	
	prev_mt->is_g_last = false; // set the previous access as not the last access
	if (is_diff_mig_period(prev_idx, cur_idx)) {
		prev_mt->is_p_last = true; // set the previous access as the last access of the previous mig period  
	} else {
		prev_mt->is_p_last = false; // set the previous access as not the last access of the previous mig period
	}

	// update the current access
	mt.prev_idx = prev_idx; // set the previous access index
	mt.is_g_first = false; // set the current access as not the first access
	if (is_diff_mig_period(prev_idx, cur_idx)) {
		mt.is_p_first = true; // set the current access as the first access of the current mig period
	} else {
		mt.is_p_first = false; // set the current access as not the first access of the current mig period
	}

out: 
	set_prev_access_idx(t.addr, cur_idx); // set the current access index
	cur_idx++; // increment the current index
	traces.push_back(mt); // push back the current trace

	return;
}

static void add_access_nodes(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		init_node(get_nidx(LAYER_ACCESS, idx, tier), mt.t, NODE_ACCESS);
	}
}

static void add_first_nodes(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (mt.is_g_first || mt.is_p_first) {
		init_node(get_nidx(LAYER_FIRST, idx), mt.t, NODE_FIRST);
	}
}

static void add_tier_choke_nodes(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (mt.is_g_first || mt.is_p_first) {
		init_node(get_nidx(LAYER_TIER_CHOKE, idx), mt.t, NODE_TIER_CHOKE);
	}
}

static void add_end_nodes(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (mt.is_g_last) {
		init_node(get_nidx(LAYER_END, idx), mt.t, NODE_END);
	}
}

static void add_interval_retention_nodes(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (mt.is_p_last && !mt.is_g_last) {
		init_node(get_nidx(LAYER_END, idx), mt.t, NODE_INTERVAL_RETENTION);
	}
}

static void add_alloc_nodes(int idx) {
	init_node(get_nidx(LAYER_ALLOC, idx), INVALID_REQ, NODE_ALLOC);
}

static void add_demo_nodes(int idx) {
	init_node(get_nidx(LAYER_DEMO, idx), INVALID_REQ, NODE_DEMO);
}

static void add_demo_choke_nodes(int idx) {
	init_node(get_nidx(LAYER_DEMO_CHOKE, idx), INVALID_REQ, NODE_DEMO_CHOKE);
}

static void add_promo_nodes(int idx) {
	init_node(get_nidx(LAYER_PROMO, idx), INVALID_REQ, NODE_PROMO);
}

static void add_promo_choke_nodes(int idx) {
	init_node(get_nidx(LAYER_PROMO_CHOKE, idx), INVALID_REQ, NODE_PROMO_CHOKE);
}

static void add_capacity_edges(int idx) {
	struct edge e;

	if (idx == 0) {
		// In the first interval, we add the capacity edge from the default capacity choke node to the alloc node
		e = init_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), get_nidx(LAYER_ALLOC, 0), INT_MAX, 0, 0, EDGE_CAPACITY);
		add_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), e);
	} else {
		// In the other intervals,
		// add the capacity edges
		// 1. from the promo node to the alloc node
		// 2. from the default capacity choke node to the promo node
		e = init_edge(get_nidx(LAYER_PROMO, idx), get_nidx(LAYER_ALLOC, idx), INT_MAX, 0, 0, EDGE_CAPACITY);
		add_edge(get_nidx(LAYER_PROMO, idx), e);

		e = init_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), get_nidx(LAYER_PROMO, idx), INT_MAX, 0, 0, EDGE_CAPACITY);
		add_edge(get_nidx(LAYER_DEF_META, my_migopt.cap_choke_nidx), e);
	}
}

static void add_demo_choke_edges(int idx) {
	struct edge e;
	e = init_edge(get_nidx(LAYER_DEMO, idx), get_nidx(LAYER_DEMO_CHOKE, idx), my_migopt.mig_traffic, 0, 0, EDGE_DEMO_CHOKE);
	add_edge(get_nidx(LAYER_DEMO, idx), e);
}

static void add_reclaimed_edges(int idx) {
	struct edge e;
	e = init_edge(get_nidx(LAYER_DEMO_CHOKE, idx), get_nidx(LAYER_PROMO, idx), INT_MAX, 0, 0, EDGE_RECLAIMED);
	add_edge(get_nidx(LAYER_DEMO_CHOKE, idx), e);

	if (idx + my_migopt.mig_period < my_migopt.nr_traces) {
		// add the reclaimed edge to the next interval's demo choke nodes
		e = init_edge(get_nidx(LAYER_DEMO_CHOKE, idx), get_nidx(LAYER_DEMO_CHOKE, idx + my_migopt.mig_period), INT_MAX, 0, 0, EDGE_RECLAIMED);
	} else {
		// if there is no next interval, we just add the reclaimed edge to the sink node
		e = init_edge(get_nidx(LAYER_DEMO_CHOKE, idx), my_migopt.sink_nidx, INT_MAX, 0, 0, EDGE_RECLAIMED);
	}
	add_edge(get_nidx(LAYER_DEMO_CHOKE, idx), e);
}

static void add_promo_choke_edges(int idx) {
	struct edge e;
	e = init_edge(get_nidx(LAYER_PROMO, idx), get_nidx(LAYER_PROMO_CHOKE, idx), my_migopt.mig_traffic/2, 0, 0, EDGE_PROMO_CHOKE);
	add_edge(get_nidx(LAYER_PROMO, idx), e);
}

static void add_retention_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	int prev_idx = mt.prev_idx;
	if (prev_idx == INVALID) return; // no previous access, so no retention edges

	if (is_diff_mig_period(prev_idx, idx)) {
		// if the previous access is on a different mig period, we do not add the retention edges
		return;
	}

	struct edge e;
	int cost;
	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		if (mt.t.type == LOAD) {
			cost = my_migopt.tier_lat_loads[tier] - my_migopt.tier_lat_loads[my_migopt.bottom_tier];
		} else if (mt.t.type == STORE) {
			cost = my_migopt.tier_lat_stores[tier] - my_migopt.tier_lat_stores[my_migopt.bottom_tier];
		}

		e = init_edge(get_nidx(LAYER_ACCESS, prev_idx, tier), get_nidx(LAYER_ACCESS, idx, tier), 1, cost, 0, EDGE_RETENTION, tier);
		add_edge(get_nidx(LAYER_ACCESS, prev_idx, tier), e);
	}
}

static void add_tier_choke_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!(mt.is_g_first || mt.is_p_first)) {
		return;
	}

	struct edge e;
	// the edge's capacity is 1 because we only allow one page exists across tiers at the same time
	e = init_edge(get_nidx(LAYER_FIRST, idx), get_nidx(LAYER_TIER_CHOKE, idx), 1, 0, 0, EDGE_TIER_CHOKE);
	add_edge(get_nidx(LAYER_FIRST, idx), e);
}

static void add_alloc_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!(mt.is_g_first || mt.is_p_first)) {
		return;
	}

	struct edge e;
	int cost;
	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		if (mt.t.type == LOAD) {
			cost = my_migopt.tier_lat_loads[tier] - my_migopt.tier_lat_loads[my_migopt.bottom_tier];
		} else if (mt.t.type == STORE) {
			cost = my_migopt.tier_lat_stores[tier] - my_migopt.tier_lat_stores[my_migopt.bottom_tier];
		}
		cost += my_migopt.tier_lat_4KB_writes[tier]; // add the 4KB write latency (alloc latency)
		e = init_edge(get_nidx(LAYER_TIER_CHOKE, idx), get_nidx(LAYER_ACCESS, idx, tier), 1, cost, 0, EDGE_ALLOC, tier);
		add_edge(get_nidx(LAYER_TIER_CHOKE, idx), e);
	}
}

static void add_alloc_reward_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!mt.is_g_first) {
		return;
	}

	struct edge e;
	int cost;
	// subtract the latency of the bottom tier
	// because we assumed the base allocation is done on the bottom tier
	cost = -my_migopt.tier_lat_4KB_writes[my_migopt.bottom_tier];
	e = init_edge(get_nidx(LAYER_ALLOC, get_period_idx(idx)), get_nidx(LAYER_FIRST, idx), 1, cost, 0, EDGE_ALLOC_REWARD);
	add_edge(get_nidx(LAYER_ALLOC, get_period_idx(idx)), e);
}

static void add_promo_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (mt.is_g_first) {
		// if the current access is the first access of the global,
		// we do not add the promo edges because there is no previous access to promote
		return;
	}

	if (!mt.is_p_first) {
		return;
	}

	struct edge e;
	int cost;
	// because there is a 4KB write latency for the allocation on the allocation edge,
	// we need to add only the 4KB read latency of the bottom tier
	cost = my_migopt.tier_lat_4KB_reads[my_migopt.bottom_tier];
	e = init_edge(get_nidx(LAYER_PROMO_CHOKE, get_period_idx(idx)), get_nidx(LAYER_FIRST, idx), 1, cost, 0, EDGE_PROMO);
	add_edge(get_nidx(LAYER_PROMO_CHOKE, get_period_idx(idx)), e);
}

static void add_demo_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!mt.is_p_last) {
		// if the current access is not the last access of the previous mig period,
		// we do not add the demo edges because there is no previous access to demo
		return;
	}

	if (get_period_idx(idx) + my_migopt.mig_period >= my_migopt.nr_traces) {
		// if the current access is the last access of the global,
		// we do not add the demo edges because there is no next access to demo
		return;
	}

	struct edge e;
	int cost;
	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		// we assum the demo always directly goes to the bottom tier
		cost = my_migopt.tier_lat_4KB_reads[tier] + my_migopt.tier_lat_4KB_writes[my_migopt.bottom_tier]; 
		e = init_edge(get_nidx(LAYER_ACCESS, idx, tier), get_nidx(LAYER_DEMO, get_period_idx(idx) + my_migopt.mig_period), 1, cost, 0, EDGE_DEMO, tier);
		add_edge(get_nidx(LAYER_ACCESS, idx, tier), e);
	}
}

static void add_end_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!mt.is_g_last) {
		// if the current access is not the last access of the global,
		// we do not add the end edges because there is no end node
		return;
	}

	struct edge e;
	e = init_edge(get_nidx(LAYER_END, idx), my_migopt.sink_nidx, 1, 0, 0, EDGE_END);
	add_edge(get_nidx(LAYER_END, idx), e);

	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		// add the end edges from the access nodes to the end node
		e = init_edge(get_nidx(LAYER_ACCESS, idx, tier), get_nidx(LAYER_END, idx), 1, 0, 0, EDGE_END, tier);
		add_edge(get_nidx(LAYER_ACCESS, idx, tier), e);
	}
}

static void add_interval_retention_edges(int idx) {
	struct migopt_trace_req &mt = traces[idx];
	if (!mt.is_p_last || mt.is_g_last) {
		// if the current access is not the last access of the previous mig period,
		// we do not add the interval retention edges because there is no interval retention node
		return;
	}

	if (mt.next_idx == INVALID) {
		printf("Next index of the current access is invalid, current index: %d\n", idx);
		abort();
	}

	struct edge e;
	e = init_edge(get_nidx(LAYER_END, idx), get_nidx(LAYER_FIRST, mt.next_idx), 1, 0, 0, EDGE_INTERVAL_RETENTION);
	add_edge(get_nidx(LAYER_END, idx), e);

	for (int tier = 0; tier < my_migopt.nr_tiers - 1; tier++) {
		// add the retention reward edges from the access nodes to the interval retention node
		// the cost of the retention reward edge is the latency of the 4KB writes of tier,
		// because we want to reward the cost for the allocation latency in the next interval
		e = init_edge(get_nidx(LAYER_ACCESS, idx, tier), get_nidx(LAYER_END, idx), 1, -my_migopt.tier_lat_4KB_writes[tier], 0, EDGE_RETENTION_REWARD, tier);
		add_edge(get_nidx(LAYER_ACCESS, idx, tier), e);
	}
}

void build_migopt_graph () {
	for (int i = 0; i <= my_migopt.nr_traces; i++) {
		if (i == 0) { // there is no trace at the "index 0", so we just add the default meta nodes and edges
			// add the default meta nodes and edges for the first interval
			add_default_meta_nodes_edges();

			// add the alloc nodes for the first interval
			add_alloc_nodes(0);
			add_capacity_edges(0); // from the default choke node to the alloc node
			continue;
		}

		add_access_nodes(i);
		add_first_nodes(i);
		add_tier_choke_nodes(i);
		add_end_nodes(i);
		add_interval_retention_nodes(i);

		if (is_idx_on_mig_period(i) && (i > 0) && (i < my_migopt.nr_traces)) {
			add_alloc_nodes(i);
			add_demo_nodes(i);
			add_demo_choke_nodes(i);
			add_promo_nodes(i);
			add_promo_choke_nodes(i);

			add_capacity_edges(i); // from the promo node to the alloc node, and from the default choke node to the promo node
			add_demo_choke_edges(i); // from the demo node to the demo choke node
			add_reclaimed_edges(i); // from the demo choke node to the promo node or the next demo choke node, or the sink node
			add_promo_choke_edges(i); // from the promo node to the promo choke node
		}

		add_retention_edges(i); // from the previous access to the current access within the same period
		add_tier_choke_edges(i); // from the first node to the tier choke node
		add_alloc_edges(i); // from the tier choke node to the access nodes of the current trace
		add_alloc_reward_edges(i); // from the alloc node to the global first node
		add_promo_edges(i); // from the promo choke node to the interval first node
		add_demo_edges(i); // from the access nodes of the current trace to the demo node
		add_end_edges(i); // from the end node to the sink node
		add_interval_retention_edges(i); // from the end node to the first node of the next interval
	}
}

static void add_rgraph_node(int node_num) {
	struct node *cur = my_migopt.nodes + node_num;
	my_migopt.rgraph.add_node(cur->req, cur->ntype);
}

static void add_rgraph_edge(int node_num, struct edge &e) {
	if (node_num != e.start) abort();
	my_migopt.rgraph.add_arc(e.start, e.next, e.cap, e.cost, e.flow, e.type, e.associated_tier);
}

static void generate_rgraph() {
	for (int i = 0; i < my_migopt.nr_nodes; i++) {
		add_rgraph_node(i);
	}

	struct node *cur;
	for (int i = 0; i < my_migopt.nr_nodes; i++) {
		cur = my_migopt.nodes + i;
		if (cur->ntype == NODE_NULL)
			continue;
		if (cur->adj->empty())
			continue;
		for (auto e : *(cur->adj)) {
			add_rgraph_edge(i, e);
		}
	}
}

static void remove_rgraph(int tier) {
	volatile bool remove_all;
	size_t sz = my_migopt.rgraph.nodes_.size();
	for (int i = 0; i < sz; i++) {
		remove_all = false;
		auto it = my_migopt.rgraph.nodes_[i].connected_arcs_.begin();
		for ( ; it != my_migopt.rgraph.nodes_[i].connected_arcs_.end(); ) {
			if ((*it)->associated_tier == tier) {
				it = my_migopt.rgraph.nodes_[i].connected_arcs_.erase(it);
			} else if ((*it)->type == EDGE_PROMO_CHOKE) {
				(*it)->cap = (*it)->cap - (*it)->flow;
				(*it)->flow = 0;
				if ((*it)->cap == 0) 
					it = my_migopt.rgraph.nodes_[i].connected_arcs_.erase(it);
				else
					it++;
			} else if (((*it)->type == EDGE_TIER_CHOKE) && ((*it)->flow == 1)) {
				remove_all = true;
				it++;
			} else {
				it++;
			}
		}

		if (remove_all) {
			auto remove_it = my_migopt.rgraph.nodes_[i].connected_arcs_.begin();
			for ( ; remove_it != my_migopt.rgraph.nodes_[i].connected_arcs_.end(); ) {
				remove_it = my_migopt.rgraph.nodes_[i].connected_arcs_.erase(remove_it);
			}

		}
	}
}

static inline void update_source_cap(int capacity, int used) {
	printf("Update cap: %d\n", capacity);
	// hard coded: 0 is the capacity choke edge
	my_migopt.rgraph.arcs_[0].flow = 0;
	my_migopt.rgraph.arcs_[0].cap = capacity - used;
}


static inline void update_item_sched(uint64_t addr, int period, int tier) {
	int org_tier = my_migopt.item_sched[addr][period];

	if (tier == my_migopt.bottom_tier && org_tier != INVALID)
		return;

	if (tier != my_migopt.bottom_tier && org_tier != INVALID && org_tier != my_migopt.bottom_tier) {
		printf("Item sched for address %lx at period %d is already set to tier %d, cannot update to tier %d\n", addr, period, org_tier, tier);
		abort();
	}

	my_migopt.item_sched[addr][period] = tier;
}


// Case 1: init item_sched for the first access of the address
// Case 2: update item_sched for the address when the alloc edge has a flow
// Case 3: update item_sched for the next period when a demo edge has a flow
void fill_item_sched(int iter) {
	struct node *cur;
	int idx, period;
	uint64_t addr;
	static unordered_set<uint64_t> g_first;
	int nr_period = get_nr_period();

	for (int i = 1; i <= my_migopt.nr_traces; i++) {
		idx = get_nidx(LAYER_TIER_CHOKE, i);
		period = get_period(i);
		cur = &my_migopt.rgraph.nodes_[idx];

		if (cur->connected_arcs_.size() != 0) {
			addr = traces[i].t.addr;
			
			if (addr != cur->req.addr) {
				printf("Address mismatch: %lx != %lx\n", addr, cur->req.addr);
				abort();
			}

			// when the address is first accessed globaly,
			// we initialize the item_sched for the address
			// the baseline tier is the bottom tier
			if (g_first.find(addr) == g_first.end()) {
				g_first.insert(addr);
				my_migopt.item_sched.insert({addr, vector<int>(nr_period, INVALID)});	
				update_item_sched(addr, period, my_migopt.bottom_tier);
			}

			// if a edge which is connected to the tier choke node
			// has a flow, we update the item_sched for the address
			// the tier is the associated tier of the edge
			for (edge *e : my_migopt.rgraph.nodes_[idx].connected_arcs_) {
				if (e->flow == 0 || e->start != idx)
					continue;

				int tier = get_tier_from_nidx(e->next);
				if (tier != e->associated_tier) {
					printf("Tier mismatch: %d != %d (assoc)\n", tier, e->associated_tier);
					abort();
				}

				if (tier != iter) {
					printf("Tier mismatch: %d != %d (iter)\n", tier, iter);
					abort();
				}
	
				if (my_migopt.item_sched.find(addr) == my_migopt.item_sched.end()) {
					abort();
				}

				update_item_sched(addr, period, tier);
 			}
		}

		// if a demotion edge's flow is not zero,
		// we update the item_sched for the address to the bottim tier
		// if the tier already filled, we do not update it
		idx = get_nidx(LAYER_DEMO, i);
		cur = &my_migopt.rgraph.nodes_[idx];
		if (cur->connected_arcs_.size() != 0) {
			for (edge *e : my_migopt.rgraph.nodes_[idx].connected_arcs_) {
				if (e->flow == 0 || e->next != idx)
					continue;

				if (e->start <= (my_migopt.nr_traces * NUM_META_LAYERS))
					continue;
	
				if (e->type != EDGE_DEMO)
					abort();
	
				addr = my_migopt.rgraph.nodes_[e->start].req.addr;

				update_item_sched(addr, period + 1, my_migopt.bottom_tier);
			}
		}
	}
}

void fill_gaps_in_item_sched() {
	int nr_period = get_nr_period();
	// fill the gaps in the item_sched
	// if an item is not scheduled in a period, we fill it with the previous tier
	for (auto &item_vec : my_migopt.item_sched) {
		for (int i = 1; i < nr_period; i++) {
			if (item_vec.second[i] == INVALID) {
				item_vec.second[i] = item_vec.second[i-1];
			}
		}
	}
}

vector<vector<int>> get_tier_size_per_period() {
	int nr_period = get_nr_period();
	int tier;
	vector<vector<int>> tier_size_per_period(nr_period, vector<int>(my_migopt.nr_tiers, 0));

	for (auto &item_vec : my_migopt.item_sched) {
		for (int i = 0; i < nr_period; i++) {
			tier = item_vec.second[i];
			if (tier != INVALID) {
				tier_size_per_period[i][tier]++;
			}
		}
	}

	return tier_size_per_period;
}

vector<int> get_room_per_tier(vector<vector<int>> &tier_size_per_period) {
	int nr_period = get_nr_period();
	vector<int> room_per_tier(my_migopt.nr_tiers, 0);

	for (int i = 0; i < my_migopt.nr_tiers; i++) {
		int tier_max = tier_size_per_period[0][i];
		for (int j = 0; j < nr_period; j++) {
			tier_max = std::max(tier_max, tier_size_per_period[j][i]);
		}
		room_per_tier[i] = my_migopt.tier_cap[i] - tier_max;
	}

	return room_per_tier;
}

vector<unordered_map<uint64_t, int>> get_remain_pages_in_bottom_tier() {
	int nr_period = get_nr_period();
	vector<unordered_map<uint64_t, int>> remain_pages(nr_period, unordered_map<uint64_t, int>());

	bool scan_done;

	for (auto &item_vec : my_migopt.item_sched) {
		scan_done = false;

		for (int i = 1; i < nr_period; i++) {
			if (item_vec.second[i] == my_migopt.bottom_tier) {
				int prev_layer = item_vec.second[i-1];

				// if a demotion occurs at this period and the dst tier is the bottom tier,
				if (prev_layer >= 0 && prev_layer < my_migopt.bottom_tier) {
					

					// if the item remains in the bottom tier for the rest of the periods,
					// we can just add it to the remain pages
					bool remain_in_last_tier = true;
					for (int j = i; j < nr_period; j++) {
						if (item_vec.second[j] != my_migopt.bottom_tier) {
							remain_in_last_tier = false;
							break;
						}
					}

					if (remain_in_last_tier) {
						remain_pages[i].insert({item_vec.first, prev_layer});
						scan_done = true;
						break; // no need to scan further, we found the remain page in the bottom tier
					}
				}
			}
			if (scan_done) {
				break;
			}
		}
	}

	return remain_pages;
}

void move_pages_in_bottom_tier(vector<unordered_map<uint64_t, int>> &remain_pages, vector<vector<int>> &tier_size_per_period, vector<int> &room_per_tier) {
	int nr_period = get_nr_period();

	for (int i = nr_period - 1; i >= 0; i--) {
		if (remain_pages[i].empty()) continue;

		// find the minimum room available in the bottom tier from period i to the end
		int min_room = INT_MAX;
		for (int j = i; j < nr_period; j++) {
			min_room = std::min(min_room, my_migopt.tier_cap[my_migopt.bottom_tier] - tier_size_per_period[j][my_migopt.bottom_tier]);
		}

		// if there is enough room in the bottom tier, we do not need to move any pages
		if (min_room >= 0) continue;

		// if there is not enough room in the bottom tier, we need to move some pages
		// we can only move pages that are demoted to the bottom tier and remain there
		// we can move at most -min_room pages from the bottom tier to the upper tiers
		int can_move = std::min(int(remain_pages[i].size()), -min_room);

		while(can_move--) {
			auto cand = remain_pages[i].begin();
			int prev_tier = cand->second;

			// we need to find a tier that is lower or equal to the previous tier
			for (int dst = my_migopt.bottom_tier - 1; dst >= 0; dst--) {
				if (prev_tier <= dst && room_per_tier[dst] > 0) {
					// we found a tier to move the page
					for (int j = i; j < nr_period; j++) {
						tier_size_per_period[j][my_migopt.bottom_tier]--;
						tier_size_per_period[j][dst]++;
						my_migopt.item_sched[cand->first][j] = dst; // update the item_sched for the address
					}
					remain_pages[i].erase(cand);
					room_per_tier[dst]--; // decrease the room available in the destination tier
					room_per_tier[my_migopt.bottom_tier]++; // increase the room available in the bottom tier
					break;
				}
			}
		}
	}
}

void move_items_from_bottom_tier() {
	int nr_period = get_nr_period();
	int tier;

	// tier_size_per_period is a 2D vector that stores the size of each tier for each period
	vector<vector<int>> tier_size_per_period = get_tier_size_per_period();

	// get the room available in each tier
	// room indicates the minimum number of available slots in each tier
	vector<int> room_per_tier = get_room_per_tier(tier_size_per_period);

	// get remain pages in the bottom tier
	// remain_pages[i] means the pages that remain in the bottom tier from period i to the end
	// this map holds the pages are demoted to the bottom tier and remain there
	// map key is the address, value is the previous tier
	vector<unordered_map<uint64_t, int>> remain_pages = get_remain_pages_in_bottom_tier();

	// move pages from the bottom tier to the upper tiers
	move_pages_in_bottom_tier(remain_pages, tier_size_per_period, room_per_tier);
}

void post_process_item_sched() {
	fill_gaps_in_item_sched();

	move_items_from_bottom_tier();
}

string print_migopt_sched() {
	// set sched file name
	string output_file = my_migopt.sched_file;
	output_file = output_file + ".migopt_mode" + to_string(my_migopt.mode) + ".sched";

	ofstream writeFile(output_file.c_str());

	// generate migration schedule 
	int nr_period = get_nr_period();
	vector<map<uint64_t,int>> mig_sched = vector(nr_period, map<uint64_t,int>());

	for (auto &item_vec : my_migopt.item_sched) {
		for (int i = 0; i < nr_period; i++) {
			if (item_vec.second[i] != INVALID)
				mig_sched[i][item_vec.first] = item_vec.second[i];
		}
	}

	for (int i = 0; i < nr_period; i++) {
		for (auto item: mig_sched[i]) {
				writeFile << "A " << i * my_migopt.mig_period << " " << item.first << " " << item.second << " " << 0 << "\n";
		}
	}

	fflush(stdout);
	writeFile.close();

	return output_file;
}

string do_migopt() {
	build_migopt_graph();

	generate_rgraph();

	uint64_t sum_cost = 0, sum_flow = 0;
	for (int iter = 0; iter < my_migopt.nr_tiers - 1; iter++) {
		printf("\nITER %d\n", iter);
		update_source_cap(my_migopt.tier_cap[iter], 0);
		auto result = my_migopt.rgraph.min_cost_max_flow(my_migopt.source_nidx, my_migopt.sink_nidx);
		printf("Estimated min cost: %d, max flow: %d\n", result[0], result[1]);
		fill_item_sched(iter);
		remove_rgraph(iter);
	}

	post_process_item_sched();

	string output_file = print_migopt_sched();
	return output_file;
}

///////////////////////////////////////////////////////
// print functions
void print_item_sched() {
	int nr_period = my_migopt.nr_traces / my_migopt.mig_period;
	for (auto &item_vec : my_migopt.item_sched) {
		printf("Addr %lx: ", item_vec.first);
		for (int i = 0; i < nr_period; i++) {
			printf("%d ", item_vec.second[i]);
		}
		printf("\n");
	}
}

static inline void print_edge(struct edge &e) {
	printf("start: %d, next: %d, cap: %d, cost: %d, flow: %d, type: %d, associated_tier: %d\n", e.start, e.next, e.cap, e.cost, e.flow, e.type, e.associated_tier);
}

static inline void print_node(int idx, struct node *n) {
	if (n->ntype == NODE_NULL) return;
	int size = n->adj->size();
	printf("Node[%d]: addr: %lu, type: %d, tier: %d, type: %d, %d edges\n", idx, n->req.addr, n->req.type, n->req.tier, n->ntype, size);
	for (int i = 0; i < size; i++) {
		print_edge(n->adj->at(i));
	}
}

static inline void print_rgraph_node(int idx) {
	int size = my_migopt.rgraph.nodes_[idx].connected_arcs_.size();
	printf("Node[%d]: %d edges\n", idx, size);
	for (int i = 0; i < size; i++) {
		print_edge(*(my_migopt.rgraph.nodes_[idx].connected_arcs_[i]));
	}
}

static void print_graph() {
	for (int i = 0; i < my_migopt.nr_nodes; i++) {
		print_node(i, my_migopt.nodes + i);
	}
}

static void print_rgraph() {
	for (int i = 0; i < my_migopt.rgraph.nodes_.size(); i++) {
		print_rgraph_node(i);
	}
}

void print_migopt() {
	printf("MigOpt Configurations\n \
			\rbottom_tier: %d\n \
			\rnr_traces: %d\n \
			\rnr_tiers: %d\n \
			\rnr_nodes: %d\n",
			my_migopt.bottom_tier,
			my_migopt.nr_traces,
			my_migopt.nr_tiers,
			my_migopt.nr_nodes);

	printf("tier_cap: ");
	for (int i = 0; i < my_migopt.nr_tiers; i++) {
		printf("%d ", my_migopt.tier_cap[i]);
	}
	printf("\n");
	printf("tier_lat_loads: ");
	for (int i = 0; i < my_migopt.nr_tiers; i++) {
		printf("%d ", my_migopt.tier_lat_loads[i]);
	}
	printf("\n");
	printf("tier_lat_stores: ");
	for (int i = 0; i < my_migopt.nr_tiers; i++) {
		printf("%d ", my_migopt.tier_lat_stores[i]);
	}
	printf("\n");

	print_graph();
	print_rgraph();

}
