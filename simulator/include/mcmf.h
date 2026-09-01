#ifndef __CHOPT_H__
#define __CHOPT_H__

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
#include "cxl_tm.h"
using namespace std;

#define MCMF_DEF 0
#define MCMF_FAULT 1

#define MCMF_SAMPLE 10000LL

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
#define E_DEMO_BAR 17
#define E_PROMO_BAR 18
#define E_AGGR_BAR 19

#define NR_ITEM_SCHED 2
#define I_LAYER 0
#define I_TYPE 1

#define FAULT_OVERHEAD 2000

struct mcmf_stat {
	uint64_t nr_accesses;
	uint64_t nr_loads;
	uint64_t nr_stores;
	uint64_t nr_promo;
	uint64_t nr_demo;
};

struct edge {
	int start;
	int next; // target
	int cap;
	int cost;
	int flow;
	int lat_load;
	int lat_store;
	int type;
	int cur_level;
	int next_level;
	
    int get_dest(int from) {  // Return the destination node if we were to traverse the arc from node `from`
        if(from == start) {
            return next;
        } else {
            return start;
        }
    }

    void add_flow(int from, int to_add) {  // Adds flow from originating vertex `from`
        if(from == start) {
            flow += to_add;
        } else {
            flow -= to_add;
        }
    }

    int get_capacity(int from) {  // Gets the capacity of the edge if the originating vertex is `from`
        if(from == start) {
            return cap - flow;
        } else {
            return flow;
        }
    }

    int get_cost_from(int from) {
        if(from == start) {
            return cost;
        } else {
            return -cost;
        }
    }
};

struct node {
	int index_;
	int layer;
	uint64_t addr;
	enum req_type type;
	int node_type;
	vector<struct edge> *adj;
	vector<edge*> connected_arcs_;
};

struct SuccessiveShortestPathFlowNetwork {

    vector<struct node> nodes_;
    deque<struct edge> arcs_;

	string str;

	void print_rnode(int idx) {
		struct node cur = nodes_[idx];
		switch(cur.node_type) {
			case N_META:
				str = "META";
				break;
			case N_DEMO:
				str = "DEMO";
				break;
			case N_PROMO:
				str = "PROMO";
				break;
			case N_ALLOC:
				str = "ALLOC";
				break;
			case N_END:
				str = "END";
				break;
			case N_AGGR:
				str = "AGGR";
				break;
			case N_AGGR_BAR:
				str = "AGGR_BAR";
				break;
			case N_REG:
				str = "REG";
				break;
			case N_PROMO_BAR:
				str = "PROMO_BAR";
				break;
			case N_DEMO_BAR:
				str = "DEMO_BAR";
				break;
			default:
				str = "";
				break;

		}
		printf("Node[%d]: layer: %d, addr: %lu, type: %d, node_type: %s, %ld edges\n", idx, cur.layer, cur.addr, cur.type, str.c_str(), cur.connected_arcs_.size());
	}

    void add_node(int layer, uint64_t addr, enum req_type type, int node_type) {
        nodes_.push_back({int(nodes_.size()), layer, addr, type, node_type, NULL, {}});
    }

    int add_arc(int start, int end, int capacity, int cost, int flow, int lat_load, int lat_store, int type, int cur_level, int next_level) {
        arcs_.push_back({start, end, capacity, cost, flow, lat_load, lat_store, type, cur_level, next_level});
        nodes_[start].connected_arcs_.push_back(&arcs_.back());
        nodes_[end].connected_arcs_.push_back(&arcs_.back());
        return (int)arcs_.size() - 1;
    }

    // Successive shortest paths min-cost max-flow algorithm
    // If there is an negative cost cycle initially, then it goes into infinite loop
    vector<int> min_cost_max_flow(int source_i, int sink_i) {

		auto now = std::chrono::system_clock::now();
		time_t now_time = std::chrono::system_clock::to_time_t(now);

		cout << "\nPotential start time: " << std::ctime(&now_time);


        // First calculate the potentials with Bellman–Ford derivative
        // It starts from a single vertex and is optimized to only operate on active vertices on each layer
        // Thus, it works more like a BFS derivative
        // Speedup: SLF (Smallest Label First) heuristic + push-time filter.
        // Result is mathematically identical; only fewer queue pushes and earlier convergence.
        vector<int> potentials(nodes_.size(), numeric_limits<int>::max());
        {

            deque<pair<int, int> > front;
            potentials[source_i] = 0;
            front.push_back({0, source_i});

            while(front.size() > 0) {
                int potential;
                int cur_i;
                std::tie(potential, cur_i) = front.front();
                front.pop_front();

                // Stale entry: a better potential for this node was found after push
                if(potential > potentials[cur_i]) {
                    continue;
                }

                for(edge* arc : nodes_[cur_i].connected_arcs_) if(arc->get_capacity(cur_i) > 0) {
                    // Traverse the arc if there is some remaining capacity
                    int new_cost = potential + arc->get_cost_from(cur_i);
                    int next_i   = arc->get_dest(cur_i);
                    // Push-time filter: skip if this path is not an improvement
                    if(new_cost >= potentials[next_i]) continue;
                    potentials[next_i] = new_cost;  // eager update
                    // SLF: promising (smaller) labels go to the front
                    if(!front.empty() && new_cost < front.front().first) {
                        front.push_front({new_cost, next_i});
                    } else {
                        front.push_back({new_cost, next_i});
                    }
                }
            }
        }

		auto cur = chrono::system_clock::now();
		time_t cur_time = chrono::system_clock::to_time_t(cur);

		cout << "Potential end time: " << std::ctime(&cur_time);

        // Next loop Dijkstra to saturate flow. Once we subtract the difference in potential, every arc will have a
        // non-negative cost in both directions, so using Dijkstra is safe

		priority_queue<tuple<int, int, edge*> > frontier;
        vector<bool> explr(nodes_.size(), false);
        vector<int> cost_to_node(nodes_.size(), -1);
        vector<edge*> arc_used(nodes_.size(), NULL);
		int path_cost;
		int cur_i;
		int next_i;

		int total_flow = 0;
		int cur_flow = 0;
		int cur_cost = 0;
        int result = 0;

        while(1) {
			fill(explr.begin(), explr.end(), false);
			fill(cost_to_node.begin(), cost_to_node.end(), -1);
			fill(arc_used.begin(), arc_used.end(), nullptr);

            frontier.push({0, source_i, NULL});

            while(frontier.size() > 0) {
                edge* cur_arc_used;
                tie(path_cost, cur_i, cur_arc_used) = frontier.top();
                path_cost = -path_cost;
                frontier.pop();

                if(!explr[cur_i]) {
                    explr[cur_i] = true;
                    arc_used[cur_i] = cur_arc_used;
                    cost_to_node[cur_i] = path_cost;

                    for(edge* arc : nodes_[cur_i].connected_arcs_) if(arc->get_capacity(cur_i) > 0) {
                        next_i = arc->get_dest(cur_i);
                        // As priority_queue is a max-heap, we use the negative of the path cost for convenience
                        // We subtract the difference of potentials from the arc cost to ensure all arcs have positive
                        // cost
                        frontier.push({
                            -path_cost - (arc->get_cost_from(cur_i) - potentials[next_i] + potentials[cur_i]),
                            next_i,
                            arc
                        });
                    }
                }
            }

            if(arc_used[sink_i] == NULL) {
				cur = chrono::system_clock::now();
				cur_time = chrono::system_clock::to_time_t(cur);
				cout << "MCMF end time: " << std::ctime(&cur_time);
                return {result, total_flow};  // We didn't find a path, so return
            }
            vector<edge*> arcs;
            int flow_pushed = numeric_limits<int>::max();
			int cur_cost = 0;
            {
                // Here we counstruct the path of arcs from source to sink
                int cur_i = sink_i;
                while(cur_i != source_i) {
                    edge* arc = arc_used[cur_i];
                    cur_i = arc->get_dest(cur_i);
                    flow_pushed = min(flow_pushed, arc->get_capacity(cur_i));
                    arcs.push_back(arc);
                }

                // Next we push flow back across all the arcs
                for(auto arc_it = arcs.rbegin(); arc_it != arcs.rend(); arc_it++) {
                    edge* arc = *arc_it;
                    arc->add_flow(cur_i, flow_pushed);
                    result += arc->get_cost_from(cur_i) * flow_pushed;
                    cur_i = arc->get_dest(cur_i);
					cur_cost += arc->get_cost_from(cur_i) * flow_pushed;
					/*
					if (arc->cur_level != 1 && arc->cur_level - arc->next_level == 1)
						printf("funcking1\n");
					if (arc->cur_level != 2 && arc->cur_level - arc->next_level == 2)
						printf("funcking2\n");

					if (arc->cur_level > arc->next_level && arc->next_level != 0) 
						printf("funcking3\n");
						*/



                }
				total_flow += flow_pushed;
            }



            // Finally, update the potentials so all edge-traversal costs remain non-negative
            for(int i=0; i<(int)nodes_.size(); i++) if(cost_to_node[i] != -1) {
                potentials[i] += cost_to_node[i];
            }
        }
    }
};

struct chopt {
	struct node *nodes;
	//vector<vector<struct edge *>> rgraph; // residual graph
	struct SuccessiveShortestPathFlowNetwork rgraph;
	int64_t base_cost;
	int *prev;
	int source;
	int sink;
	int base_layer;
	unordered_map<uint64_t, int> prev_accesses;
	int nr_traces;
	int nr_tiers;
	int nr_nodes;
	int cur;
	int mcmf_type;
	int nr_cache_pages[MAX_NR_TIERS];
	int lat_loads[MAX_NR_TIERS];
	int lat_stores[MAX_NR_TIERS];
	int lat_4KB_reads[MAX_NR_TIERS];
	int lat_4KB_writes[MAX_NR_TIERS];
	struct mcmf_stat stats[MAX_NR_TIERS];
	struct mcmf_stat g_stat;
};

struct migopt {
	struct node *nodes;
	//vector<vector<struct edge *>> rgraph; // residual graph
	struct SuccessiveShortestPathFlowNetwork rgraph;
	int64_t base_cost;
	int *prev;
	int source;
	int sink;
	int chock;
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
	int mcmf_type;
	int nr_cache_pages[MAX_NR_TIERS];
	int lat_loads[MAX_NR_TIERS];
	int lat_stores[MAX_NR_TIERS];
	int lat_4KB_reads[MAX_NR_TIERS];
	int lat_4KB_writes[MAX_NR_TIERS];
	struct mcmf_stat stats[MAX_NR_TIERS];
	struct mcmf_stat g_stat;
	int nr_items;
	int nr_period;
	char *alloc_file;

	// for the cache schedule
	vector<vector<unordered_map<uint64_t,int>>> cache_sched;

	// layer, type (-1: unknown, 0: ALLOC, 1: PROMO, 2: HIT, 3: DEMO), local accesses, initial alloc time, local alloc time, reuse distance, inter ref
	unordered_map<uint64_t,vector<vector<int>>> item_sched;

	unordered_set<uint64_t> g_first;
	vector<int> reuse_dist;
	std::list<uint64_t> lru;

};


void init_chopt (int mcmf_type, int nr_traces, int nr_tiers, int *nr_cache_sizes, int *lat_loads, int *lat_stores, int *lat_4KB_reads, int *lat_4KB_writes);
void destroy_chopt(void);
void chopt_generate_graph(uint64_t addr, enum req_type type);
void chopt_generate_rgraph(uint64_t addr, enum req_type type);
uint64_t chopt_do_optimal(void);
void print_chopt();


void init_migopt (int mcmf_type, int nr_traces, int period, int mig_traffic, int nr_tiers, int *nr_cache_sizes, int *lat_loads, int *lat_stores, int *lat_4KB_reads, int *lat_4KB_writes, char *alloc_file);
void destroy_migopt(void);
void migopt_generate_graph(uint64_t addr, enum req_type type);
void migopt_generate_rgraph(uint64_t addr, enum req_type type);
uint64_t migopt_do_optimal(void);
void print_migopt();
void migopt_analysis_graph(int iter);
void migopt_print_cache_sched();


#endif
