#include <stdio.h>
#include <algorithm>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <tuple>
#include <queue>
#include <numeric>
#include <algorithm>
#include <cmath>
#include "analysis.h"

using namespace std;

static struct analysis analysis;

static inline void print_node(int idx, struct anode *n) {
	if (n->node_type == -1) return;
	printf("Node[%d]: layer: %d, addr: %lu, type: %d, node_type: %d\n", idx, n->layer, n->addr, n->type, n->node_type);
}

static void init_node (struct anode *n, int layer, uint64_t addr, enum trace_type type, int node_type, int reuse_dist, int inter_ref, int alloc_time, int alloc_dist, int ref_cnt, int access_time) {
	if (node_type == N_NOP || node_type != N_REG)
		abort();
	n->layer = layer;
	n->addr = addr;
	n->type = type;
	n->node_type = node_type;
	n->reuse_dist = reuse_dist;
	n->inter_ref = inter_ref;
	n->alloc_time = alloc_time;
	n->alloc_dist = alloc_dist;
	n->ref_cnt = ref_cnt;
	n->access_time = access_time;
}

static void destroy_node (struct anode *n) {
	return;
}

//void init_analysis (int nr_traces, int period, int mig_traffic, int nr_tiers, int *nr_cache_pages, int *lat_loads, int *lat_stores, int *lat_4KB_reads, int *lat_4KB_writes) {
void init_analysis (struct sim_cfg &scfg) {
	analysis.nr_traces = scfg.nr_sampled_traces;
	int nr_nodes = analysis.nr_traces + 1;
	analysis.nodes = (struct anode *)malloc(sizeof(struct anode) * nr_nodes);
	analysis.nr_tiers = scfg.nr_tiers;
	analysis.period = scfg.mig_period;
	analysis.mig_traffic = scfg.mig_traffic == -1 ? INT_MAX : scfg.mig_traffic;

	printf("analysis nodes: %d, traces: %d\n", nr_nodes, analysis.nr_traces);

	for (int i = 0; i < nr_nodes; i++) {
		(analysis.nodes + i)->node_type = N_NOP;
	}

	for (int i = 0; i < analysis.nr_tiers; i++) { // store reverse order
		analysis.nr_cache_pages[i] = scfg.tier_cap[i];
		analysis.lat_loads[i] = scfg.tier_lat_loads[i];
		analysis.lat_stores[i] = scfg.tier_lat_stores[i];
		analysis.lat_4KB_reads[i] = scfg.tier_lat_4KB_reads[i];
		analysis.lat_4KB_writes[i] = scfg.tier_lat_4KB_writes[i];

	}
	analysis.cur = 1; // insert a node from index 1 --> source node: 0
	analysis.base_layer = scfg.nr_tiers - 1;
	analysis.nr_nodes = nr_nodes;
}

void destroy_analysis() {
	free(analysis.nodes);
}

static void print_graph() {
	for (int i = 0; i < analysis.nr_nodes; i++) {
		print_node(i, analysis.nodes + i);
	}
}

void print_analysis() {
	printf("OPT Configurations\n \
			\rbase_layer: %d\n \
			\rnr_traces: %d\n \
			\rnr_tiers: %d\n \
			\rnr_nodes: %d\n \
			\rcur: %d\n",
			analysis.base_layer,
			analysis.nr_traces,
			analysis.nr_tiers,
			analysis.nr_nodes,
			analysis.cur);

	printf("nr_cache_pages: ");
	for (int i = 0; i < analysis.nr_tiers; i++) {
		printf("%d ", analysis.nr_cache_pages[i]);
	}
	printf("\n");
	printf("lat_loads: ");
	for (int i = 0; i < analysis.nr_tiers; i++) {
		printf("%d ", analysis.lat_loads[i]);
	}
	printf("\n");
	printf("lat_stores: ");
	for (int i = 0; i < analysis.nr_tiers; i++) {
		printf("%d ", analysis.lat_stores[i]);
	}
	printf("\n");

	print_graph();
}

static inline bool is_bottom (int layer) {
	if (layer == analysis.nr_tiers - 1)
		return true;
	return false;
}

void analysis_add_trace(struct trace_req &t) {
	uint64_t addr = t.addr;
	enum trace_type type = t.type;
	analysis.g_stat.nr_accesses++;
	if (type == LOAD) analysis.g_stat.nr_loads++;
	else analysis.g_stat.nr_stores++;
	
	int reuse_dist, inter_ref, alloc_time, alloc_dist;

	auto lru_it = find(analysis.lru.begin(), analysis.lru.end(), addr);
	if (lru_it == analysis.lru.end()) {
		reuse_dist = INT_MAX;
	} else {
		reuse_dist = distance(analysis.lru.begin(), lru_it) + 1;
		analysis.lru.erase(lru_it);
	}
	analysis.lru.push_front(addr);

	auto it = analysis.prev_accesses.find(addr);
	if (it != analysis.prev_accesses.end()) { // add a retension link
		inter_ref = analysis.cur - it->second;

		it->second = analysis.cur;
		//analysis
		analysis.total_accesses[addr].push_back(analysis.cur);
		alloc_time = analysis.total_accesses[addr][0];
		alloc_dist = analysis.cur - alloc_time;
	} else {
		inter_ref = INT_MAX;
		alloc_time = analysis.cur;
		alloc_dist = 0;

		analysis.prev_accesses.insert({addr, analysis.cur});
		analysis.total_accesses.insert({addr, vector<int>(1, analysis.cur)});
		analysis.total_inter_ref.insert({analysis.cur, vector<int>(1, INT_MAX)});
	}

	struct anode *cur = &analysis.nodes[analysis.cur];
	init_node(cur, -1, addr, type, N_REG, reuse_dist, inter_ref, alloc_time, alloc_dist, analysis.total_accesses[addr].size(), analysis.cur);
	/*
	insert_new_node(layer, addr, type, node_type, reuse_dist, inter_ref); // insert a new node and links
	*/
	analysis.cur++;
}

static void init_item_sched(uint64_t addr, int nr_period) {
	analysis.item_sched.insert({addr, vector<struct mig_stat>(nr_period)});
	for (int i = 0; i < nr_period; i++) {
		analysis.item_sched[addr][i].layer = -1;
		analysis.item_sched[addr][i].type = -1;
	}
}

float pstat_average(std::vector<int> const& _v){
	vector<int> v;
	for (auto num : _v) {
		if (num != INT_MAX)
			v.push_back(num);
	}

    if(v.empty()){
        return -1.0;
    }

    auto const count = static_cast<float>(v.size());
    return std::reduce(v.begin(), v.end()) / count;
}

float pstat_max(std::vector<int> const& _v){
	vector<int> v;
	for (auto num : _v) {
		if (num != INT_MAX)
			v.push_back(num);
	}

    if(v.empty()){
        return -1.0;
    }

	return (float)(*std::max_element(v.begin(), v.end()));
}

float pstat_min(std::vector<int> const& _v){
	vector<int> v;
	for (auto num : _v) {
		if (num != INT_MAX)
			v.push_back(num);
	}

    if(v.empty()){
        return -1.0;
    }

	return (float)(*std::min_element(v.begin(), v.end()));
}

void print_panal(int nr_period) {
	map<pair<int,int>,string> mig_order;
	mig_order.insert({{1,0}, "P1"});
	mig_order.insert({{2,0}, "P2"});
	mig_order.insert({{2,1}, "P3"});
	mig_order.insert({{3,0}, "P4"});
	mig_order.insert({{3,1}, "P5"});
	mig_order.insert({{3,2}, "P6"});

	mig_order.insert({{0,1}, "D1"});
	mig_order.insert({{0,2}, "D2"});
	mig_order.insert({{0,3}, "D3"});
	mig_order.insert({{1,2}, "D4"});
	mig_order.insert({{1,3}, "D5"});
	mig_order.insert({{2,3}, "D6"});

	struct page_anal *panal;
	string mig_type;
	for (int i = 0; i < nr_period; i++) {
		for (auto addr_panal : analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			panal = addr_panal.second;

			mig_type = mig_order.count(panal->next_action) ? mig_order[panal->next_action] : "REG";

		}
	}
}

void print_pstat(int nr_period, int type) {
	switch(type) {
		case I_TYPE_TOTAL:
			printf("TOTAL PSTAT\n");
			break;

		case I_TYPE_ALLOC:
			printf("ALLOC PSTAT\n");
			break;

		case I_TYPE_REMAIN:
			printf("REMAIN PSTAT\n");
			break;

		case I_TYPE_PROMO:
			printf("PROMO PSTAT\n");
			break;

		case I_TYPE_DEMO:
			printf("DEMO PSTAT\n");
			break;
	}
	printf("AVG last_access_time_dist\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].last_access_time_dist_stat.mean);
		}
		printf("\n");
	}

	printf("AVG next_access_dist\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].next_access_dist_stat.mean);
		}
		printf("\n");
	}

	printf("AVG freq\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].freq_stat.mean);
		}
		printf("\n");
	}

	printf("AVG future_freq (inclusive)\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].future_freq_inclusive_stat.mean);
		}
		printf("\n");
	}

	printf("AVG future_freq (exclusive)\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].future_freq_exclusive_stat.mean);
		}
		printf("\n");
	}


	printf("AVG future_freq_same_tier\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].future_freq_same_tier_stat.mean);
		}
		printf("\n");
	}


	printf("AVG hotness\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].hotness_stat.mean);
		}
		printf("\n");
	}

	printf("AVG alloc_dist\n");
	for (int i = 0; i <= analysis.nr_tiers; i++) {
		if (i < analysis.nr_tiers)
			printf("Tier-%d ", i);
		else
			printf("Total ");
		for (int j = 0; j < nr_period; j++) {
			printf("%.2f ", analysis.total_page_stat[j][i][type].alloc_dist_stat.mean);
		}
		printf("\n");
	}

}

void _print_pstat_per_path(int nr_period) {
	printf("AVG last_access_time_dist\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].last_access_time_dist_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].last_access_time_dist_stat.median);
		}
		printf("\n");
	}

	printf("AVG next_access_dist\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].next_access_dist_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].next_access_dist_stat.median);
		}
		printf("\n");
	}

	printf("AVG freq\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].freq_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].freq_stat.median);
		}
		printf("\n");
	}

	printf("AVG future_freq (inclusive)\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].future_freq_inclusive_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].future_freq_inclusive_stat.median);
		}
		printf("\n");
	}

	printf("AVG future_freq (exclusive)\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].future_freq_exclusive_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].future_freq_exclusive_stat.median);
		}
		printf("\n");
	}

	printf("AVG future_freq_same_tier\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].future_freq_same_tier_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].future_freq_same_tier_stat.median);
		}
		printf("\n");
	}


	printf("AVG cur_freq\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].cur_freq_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].cur_freq_stat.median);
		}
		printf("\n");
	}

	printf("AVG hotness\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].hotness_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%2.f ", analysis.page_stat_per_path[nr_period][key].hotness_stat.median);
		}
		printf("\n");
	}

	printf("AVG alloc_dist\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%.2f ", analysis.page_stat_per_path[k][key].alloc_dist_stat.mean);
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%.2f ", analysis.page_stat_per_path[nr_period][key].alloc_dist_stat.median);
		}
		printf("\n");
	}

	printf("Count\n");
	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%d->%d ", i, j);
			for (int k = 0; k <= nr_period; k++) {
				printf("%ld ", analysis.page_stat_per_path[k][key].freqs.size());
			}
			printf("\n");
		}
	}

	for (int i = -1; i < analysis.nr_tiers; i++) {
		for (int j = 0; j < analysis.nr_tiers; j++) {
			pair<int,int> key = {i,j};
			printf("%ld ", analysis.page_stat_per_path[nr_period][key].freqs.size());
		}
		printf("\n");
	}

	printf("Future window\n");
	for (int window = 0; window < nr_period; window++) {
		printf("Window=%d\n", window);
		for (int i = -1; i < analysis.nr_tiers; i++) {
			for (int j = 0; j < analysis.nr_tiers; j++) {
				pair<int,int> key = {i,j};
				printf("%.2f ", analysis.page_stat_per_path[nr_period][key].future_freq_window_stat[window].median);
			}
			printf("\n");
		}

	}

	printf("Future same tier window\n");
	for (int window = 0; window < nr_period; window++) {
		printf("Window=%d\n", window);
		for (int i = -1; i < analysis.nr_tiers; i++) {
			for (int j = 0; j < analysis.nr_tiers; j++) {
				pair<int,int> key = {i,j};
				printf("%.2f ", analysis.page_stat_per_path[nr_period][key].future_freq_same_tier_window_stat[window].median);
			}
			printf("\n");
		}

	}
}

void print_pstat_per_path(int nr_period) {
	printf("PSTAT PER PATH\n");
	_print_pstat_per_path(nr_period);
	fflush(stdout);
}

// percentile helper
float compute_percentile(const std::vector<int>& v, float percentile) {
    if (v.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    // clamp to [0, size-1]
    size_t index = static_cast<size_t>(std::ceil(percentile / 100.0f * v.size())) - 1;
    index = std::max<size_t>(0, std::min(index, v.size() - 1));
    return static_cast<float>(v[index]);
}

// median helper
float compute_median(const std::vector<int>& v) {
    if (v.empty()) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    size_t n = v.size();
    if (n % 2 == 0) {
        // even count: mean of the two middle values
        size_t mid = n / 2;
        return (static_cast<float>(v[mid - 1]) + static_cast<float>(v[mid])) / 2.0f;
    } else {
        // odd count: middle value
        return static_cast<float>(v[n / 2]);
    }
}

// population skewness
float compute_skewness(const std::vector<int>& v, float mean, float stddev) {
    if (v.empty() || stddev == 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float sum_cubed_diff = 0.0f;
    for (int x : v) {
        float diff = x - mean;
        sum_cubed_diff += diff * diff * diff;
    }
    // population skewness = (1/N) * sum((x_i - mean)^3) / stddev^3
    return (sum_cubed_diff / static_cast<float>(v.size())) / (stddev * stddev * stddev);
}

// population kurtosis
float compute_kurtosis(const std::vector<int>& v, float mean, float stddev) {
    if (v.empty() || stddev == 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    float sum_fourth_diff = 0.0f;
    for (int x : v) {
        float diff = x - mean;
        sum_fourth_diff += diff * diff * diff * diff;
    }
    // population kurtosis = (1/N) * sum((x_i - mean)^4) / stddev^4
    return (sum_fourth_diff / static_cast<float>(v.size())) / (stddev * stddev * stddev * stddev);
}

struct page_stat_def compute_statistics(const std::vector<int>& _v) {
    // skip INT_MAX entries
    std::vector<int> v;
    v.reserve(_v.size());
    for (int num : _v) {
        if (num != INT_MAX) {
            v.push_back(num);
        }
    }

    if (v.empty()) {
        return {
            -1.0f,  // mean
            INT_MAX,// min
            INT_MIN,// max
            -1.0f,  // p5
            -1.0f,  // p95
            -1.0f,  // variance
            -1.0f,  // stddev
            -1.0f,  // median
            -1.0f,  // q1
            -1.0f,  // q3
            -1.0f,  // iqr
            -1.0f,  // skewness
            -1.0f,  // kurtosis
            0,      // count
            0.0f    // sum
        };
    }


    std::sort(v.begin(), v.end());


    size_t count = v.size();
    float sum = std::accumulate(v.begin(), v.end(), 0.0f);


    float mean = sum / static_cast<float>(count);


    float variance = 0.0f;
    for (float num : v) {
        float diff = num - mean;
        variance += diff * diff;
    }
    variance /= static_cast<float>(count); // population variance
    float stddev = std::sqrt(variance);


    int minVal = v.front();
    int maxVal = v.back();

    // percentiles
    float p5  = compute_percentile(v, 5.0f);
    float p95 = compute_percentile(v, 95.0f);


    float median = compute_median(v);


    float q1 = compute_percentile(v, 25.0f);
    float q3 = compute_percentile(v, 75.0f);
    float iqr = q3 - q1;

    // skewness, kurtosis
    float skewness  = compute_skewness(v, mean, stddev);
    float kurtosis  = compute_kurtosis(v, mean, stddev);

    return {
        mean,
        minVal,
        maxVal,
        p5,
        p95,
        variance,
        stddev,
        median,
        q1,
        q3,
        iqr,       // IQR
        skewness,
        kurtosis,
        count,
        sum
    };
}

static void post_proc_panal (uint64_t nr_period) {
	uint64_t addr; 
	struct page_anal *prev_panal, *panal, *next_panal;

	for (auto addr_vec : analysis.access_per_addr) {
		int nr_prev_access = 0;
		struct anode *req, *next_req;
		addr = addr_vec.first;
		auto req_vec = addr_vec.second;
		for (int i = 0; i < req_vec.size(); i++) {
			req = req_vec[i];
			next_req = (i == req_vec.size() - 1) ? NULL : req_vec[i + 1];

			panal = analysis.total_page_anal[req->period_idx][analysis.nr_tiers][I_TYPE_TOTAL][addr];
			panal->last_access_time = req->access_time;
			panal->last_access_time_dist = (req->period_idx + 1) * analysis.period - panal->last_access_time;

			panal->inter_ref = req->inter_ref;
			panal->next_access_dist = next_req ? next_req->access_time - ((req->period_idx + 1) * analysis.period) : INT_MAX;
			if (panal->next_access_dist < 0) panal->next_access_dist = INT_MAX;

			panal->freq = req->ref_cnt;
			if (panal->freq == 0) abort();
			panal->future_freq_exclusive = req_vec.size() - panal->freq;

			if (i == 0) {
				panal->alloc_time = req->access_time;
				panal->alloc_dist = (panal->alloc_time/analysis.period + 1) * analysis.period - panal->alloc_time;
			}
		}
	}

	for (int i = 0; i < nr_period; i++) {
		for (auto addr_panal : analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;

			if (panal->type == I_TYPE_ALLOC) {
				panal->cur_freq = panal->freq;
				panal->hotness = panal->cur_freq;
				panal->future_freq_inclusive = panal->cur_freq + panal->future_freq_exclusive;
				continue;
			}

			if (panal->type > I_TYPE_DEMO || panal->type < I_TYPE_ALLOC)
				abort();

			prev_panal = analysis.total_page_anal[i-1][analysis.nr_tiers][I_TYPE_TOTAL][addr];
			if (panal->last_access_time == -1) {
				panal->last_access_time = prev_panal->last_access_time;
				panal->last_access_time_dist = prev_panal->last_access_time_dist + analysis.period;

				panal->inter_ref = prev_panal->inter_ref;
				panal->next_access_dist = prev_panal->next_access_dist == INT_MAX ? INT_MAX : prev_panal->next_access_dist - analysis.period;
				if (panal->next_access_dist < 0) abort();

				panal->freq = prev_panal->freq;
				panal->future_freq_exclusive = prev_panal->future_freq_exclusive;
			}

			panal->cur_freq = panal->freq - prev_panal->freq;
			panal->future_freq_inclusive = panal->cur_freq + panal->future_freq_exclusive;

			if ((i % COOLING_PERIOD) == 0) {
				panal->hotness = prev_panal->hotness / 2 + panal->cur_freq;
			} else {
				panal->hotness = prev_panal->hotness + panal->cur_freq;
			}

			if (panal->type == I_TYPE_DEMO) {
				panal->hotness = 0;
			}

			panal->alloc_time = prev_panal->alloc_time;
			panal->alloc_dist = i * analysis.period - panal->alloc_time;
		}
	}

	for (int i = nr_period - 2; i >= 0; i--) {
		for (auto addr_panal : analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;
			next_panal = analysis.total_page_anal[i+1][analysis.nr_tiers][I_TYPE_TOTAL][addr];

			if (i == nr_period - 2) {
				next_panal->future_freq_same_tier = next_panal->cur_freq;
			}

			if (next_panal->cur_layer == panal->cur_layer) {
				panal->future_freq_same_tier = next_panal->future_freq_same_tier + panal->cur_freq;
			} else {
				panal->future_freq_same_tier = panal->cur_freq;
			}
		}
	}


	// calculate window freq
	int max_window = nr_period;
	int sum;
	for (int i = 0; i < nr_period; i++) {
		for (auto addr_panal: analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;

			panal->future_freq_window = vector<int>(max_window, INT_MAX);
			panal->future_freq_window[0] = panal->cur_freq;

			for (int j = 1; j < max_window && i + j < nr_period; j++) {
				next_panal = analysis.total_page_anal[i + j][analysis.nr_tiers][I_TYPE_TOTAL][addr];
				sum = panal->future_freq_window[j-1] + next_panal->cur_freq;
				panal->future_freq_window[j] = sum;
			}

			for (int j = 0; j < nr_period; j++) {
				if (panal->future_freq_window[j] == INT_MAX)
					panal->future_freq_window[j] = panal->future_freq_window[j-1];
			}
		}
	}

	for (int i = 0; i < nr_period; i++) {
		for (auto addr_panal: analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;

			panal->future_freq_same_tier_window = vector<int>(max_window, INT_MAX);
			panal->future_freq_same_tier_window[0] = panal->cur_freq;


			for (int j = 1; j < max_window && i + j < nr_period; j++) {
				next_panal = analysis.total_page_anal[i + j][analysis.nr_tiers][I_TYPE_TOTAL][addr];
				if (next_panal->cur_layer != panal->cur_layer) {
					break;
				}

				sum = panal->future_freq_same_tier_window[j-1] + next_panal->cur_freq;
				panal->future_freq_same_tier_window[j] = sum;
			}

			for (int j = 0; j < nr_period; j++) {
				if (panal->future_freq_same_tier_window[j] == INT_MAX)
					panal->future_freq_same_tier_window[j] = panal->future_freq_same_tier_window[j-1];
			}
		}
	}

}

void calc_expected_layer (uint64_t nr_period, int window, int promo_coeff, int demo_coeff, bool only_use_hotness=false) {
	map<pair<int,uint64_t>, pair<struct page_anal *,int>> hist;

	uint64_t addr;
	int hotness;
	int promo_cost, promo_benefit, gain;
	struct page_anal *prev_panal, *panal, *next_panal;
	int nr_print;
	int nr_promo_done;
	int nr_larger_than_zero;
	vector<pair<int,int>> demo_cost = vector<pair<int,int>>(analysis.nr_tiers, {0,0});

	int nr_promo; 
	vector<int> nr_promo_by_tier(analysis.nr_tiers, 0);

	int total_nr_promo = 0, total_nr_promo_succ = 0, total_nr_larger_than_zero = 0;
	vector<pair<int,int>> total_nr_promo_by_tier(analysis.nr_tiers, {0,0});
	vector<int> nr_promo_succ(analysis.nr_tiers, 0);
	vector<int> nr_promo_try(analysis.nr_tiers, 0);
	vector<int> nr_promo_opt(analysis.nr_tiers, 0);

	for (int i = 0; i < nr_period - 1; i++) {
		
		for (int promo_layer = 0; promo_layer < analysis.nr_tiers - 1; promo_layer++) {
			auto &pstat = analysis.total_page_stat[i][promo_layer][I_TYPE_TOTAL];
			auto &next_pstat = analysis.total_page_stat[i+1][promo_layer][I_TYPE_ALLOC];
			int hotness_min = pstat.future_freq_window_stat[window].min;
			int hotness_max = pstat.future_freq_window_stat[window].max;

			if (hotness_min == -1) {
				continue;
			}

			hotness_min = next_pstat.future_freq_window_stat[window].min == -1 ? hotness_min : std::min(next_pstat.future_freq_window_stat[window].min, hotness_min);
			hotness_max = next_pstat.future_freq_window_stat[window].max == -1 ? hotness_max : std::max(next_pstat.future_freq_window_stat[window].max, hotness_max);

			int demo_cost_min = analysis.lat_4KB_reads[promo_layer] + analysis.lat_4KB_writes[promo_layer-1];
			int demo_cost_max = analysis.lat_4KB_reads[promo_layer] + analysis.lat_4KB_writes[analysis.nr_tiers - 1];
			//if (analysis.total_page_anal[i][next_level][I_TYPE_TOTAL].size() == 
			int demo_penalty_min = abs((analysis.lat_loads[promo_layer] - analysis.lat_loads[promo_layer - 1]) * hotness_min);
			int demo_penalty_max = abs((analysis.lat_loads[promo_layer] - analysis.lat_loads[analysis.nr_tiers - 1]) * hotness_max);

			demo_cost[promo_layer] = {demo_cost_min + demo_penalty_min, demo_cost_max + demo_penalty_max};
		}


		for (auto addr_panal : analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;
			next_panal = analysis.total_page_anal[i+1][analysis.nr_tiers][I_TYPE_TOTAL][addr];

			if (panal->cur_layer == 0) // no need to promote
				continue;

			hotness = next_panal->future_freq_window[window];

			if (only_use_hotness) {
				hist.insert({{hotness, addr}, {panal,-1}});
				continue;
			}

			for (int promo_layer = panal->cur_layer - 1; promo_layer >= 0; promo_layer--) {
				promo_benefit = hotness * (analysis.lat_loads[panal->cur_layer] - analysis.lat_loads[promo_layer]);
				promo_cost = analysis.lat_4KB_reads[panal->cur_layer] + analysis.lat_4KB_writes[promo_layer]; 
				int promo_cost_coeff = promo_cost * promo_coeff / 100;
				int demo_cost_coeff = demo_coeff == -1 ? 0 : demo_cost[promo_layer].first + ((demo_cost[promo_layer].second - demo_cost[promo_layer].first) * demo_coeff / 100);
				gain = promo_benefit - promo_cost_coeff - demo_cost_coeff;

				hist.insert({{gain, addr}, {panal,promo_layer}});
			}
		}

		for (int tier = 0; tier < analysis.nr_tiers - 1; tier++) {
			int demo_cost_coeff = demo_cost[tier].first + ((demo_cost[tier].second - demo_cost[tier].first) * demo_coeff / 100);
		}

		nr_promo = 0;
		for (int j = 0; j < analysis.nr_tiers; j++) {

			nr_promo_by_tier[j] = analysis.nr_cache_pages[j]
				- analysis.total_page_anal[i+1][j][I_TYPE_REMAIN].size()
				- analysis.total_page_anal[i+1][j][I_TYPE_ALLOC].size();


			total_nr_promo_by_tier[j].first += nr_promo_by_tier[j];
			nr_promo_opt[j] += analysis.total_page_anal[i+1][j][I_TYPE_PROMO].size();
			nr_promo += nr_promo_by_tier[j];
		}

		nr_print = std::max(10, nr_promo);
		nr_promo_done = 0;
		nr_larger_than_zero = 0;

		for (auto iter = hist.rbegin(); iter != hist.rend();) {
			gain = iter->first.first;
			addr = iter->first.second;
			panal = iter->second.first;
			next_panal = analysis.total_page_anal[i+1][analysis.nr_tiers][I_TYPE_TOTAL][addr];
			if (panal->expected_layer != -1) {
				iter++;
				continue;
			}

			if (gain <= 0)
				break;

			if (iter->second.second == -1) {
				int tier = 0;
				for (; tier < analysis.nr_tiers; tier++)
					if (nr_promo_by_tier[tier] > 0)
						break;
				iter->second.second = tier;
			}

			if (iter->second.second >= panal->cur_layer) {
				iter++;
				continue;
			}




			nr_promo = analysis.mig_traffic == -1 ? nr_promo : std::min(nr_promo, analysis.mig_traffic);
			if (nr_promo_done < nr_promo) {
				if (nr_promo_by_tier[iter->second.second] <= 0) {
					iter++;
					continue;
				}
				panal->expected_layer = iter->second.second; // promo_layer;
				nr_promo_done++;
				nr_promo_by_tier[iter->second.second]--;
				nr_promo_try[iter->second.second++]++;
				if (panal->expected_layer == next_panal->cur_layer) {
					nr_promo_succ[panal->expected_layer]++;
				}
			} else
				break;

			

			int demo_cost_coeff = demo_cost[iter->second.second].first + ((demo_cost[iter->second.second].second - demo_cost[iter->second.second].first) * demo_coeff / 100);
			nr_print--;
			iter++;
		}

		total_nr_larger_than_zero += nr_larger_than_zero;


		hist.clear();
	}

	int total_nr_promo_try = 0, total_nr_promo_opt = 0;

	for (int i = 0; i < analysis.nr_tiers; i++) {
		total_nr_promo_try += nr_promo_try[i];
		total_nr_promo_succ += nr_promo_succ[i];
		total_nr_promo_opt += nr_promo_opt[i];
	}

	printf("Window: %d, use_hotness: %d, promo_coeff: %d, domo_coeff: %d, Total nr_try: %d, nr_promo_succ: %d(%d%%), total_larger_than_zero: %d (succ: %d%%), total_opt: %d (succ: %d%%)\n",
			window,
			only_use_hotness,
			promo_coeff,
			demo_coeff,
			total_nr_promo_try,
			total_nr_promo_succ,
			total_nr_promo_try ? total_nr_promo_succ * 100 / total_nr_promo_try : 100,
			total_nr_larger_than_zero,
			total_nr_larger_than_zero ? total_nr_promo_succ * 100 / total_nr_larger_than_zero : 100,
			total_nr_promo_opt,
			total_nr_promo_opt ? total_nr_promo_succ * 100 / total_nr_promo_opt : 100);

	for (int i = 0; i < analysis.nr_tiers; i++) {
		printf("tier-%d succ: %d out of %d tries (%d%%), opt: %d (succ: %d%%)\n",
				i,
				nr_promo_succ[i],
				nr_promo_try[i],
				nr_promo_try[i] ? nr_promo_succ[i] * 100 / nr_promo_try[i] : 100, 
				nr_promo_opt[i],
				nr_promo_opt[i] ? nr_promo_succ[i] * 100 / nr_promo_opt[i] : 100);
	}


	for (int i = 0; i < nr_period; i++) {
		
		for (auto addr_panal : analysis.total_page_anal[i][analysis.nr_tiers][I_TYPE_TOTAL]) {
			addr = addr_panal.first;
			panal = addr_panal.second;

			panal->expected_layer = -1;
	
		}
	}
}

static void print_period_info(int period) {
	uint64_t addr;
	struct page_anal *panal;
	string type;
	printf("PERIOD_INFO: %d\n", period);
	printf("addr cur_layer prev_layer type past_recency past_freq future_recency future_freq alloc_time\n");
	for (auto addr_panal : analysis.total_page_anal[period][analysis.nr_tiers][I_TYPE_TOTAL]) {
		addr = addr_panal.first;
		panal = addr_panal.second;
		
		if (panal->prev_layer == -1) {
			type = "alloc";
		} else if (panal->prev_layer > panal->cur_layer) {
			type = "promo";
		} else if (panal->prev_layer < panal->cur_layer) {
			type = "demo";
		} else {
			type = "remain";
		}

		printf("%lu %d %d %s %d %d %d %d %d\n",
				panal->addr,
				panal->cur_layer,
				panal->prev_layer,
				type.c_str(),
				panal->last_access_time_dist,
				panal->freq,
				panal->next_access_dist,
				panal->future_freq_same_tier,
				panal->alloc_dist);
	}
}

// =============================================================
// CSV output for the analysis figures (Fig. 5-7 and Fig. 9)
// Each function writes one CSV; plotting is done externally.
// =============================================================

static std::string g_fig_dir;

static FILE* fig_open(const std::string &name, const std::string &header) {
	if (g_fig_dir.empty()) {
		g_fig_dir = "./results/figs";
		std::string cmd = "mkdir -p " + g_fig_dir;
		(void)!system(cmd.c_str());
	}
	std::string path = g_fig_dir + "/" + name;
	FILE *f = fopen(path.c_str(), "w");
	if (!f) { perror(path.c_str()); return nullptr; }
	fprintf(f, "%s\n", header.c_str());
	return f;
}

// Fig 5: allocation distribution — how many new pages allocated to each tier per period
static void dump_fig5_alloc(int nr_period) {
	FILE *f = fig_open("fig5_alloc_dist.csv", "period,tier,alloc_count");
	if (!f) return;
	for (int p = 0; p < nr_period; p++) {
		for (int t = 0; t < analysis.nr_tiers; t++) {
			int cnt = 0;
			auto it = analysis.alloc_addr_by_period[p].find(t);
			if (it != analysis.alloc_addr_by_period[p].end()) cnt = (int)it->second.size();
			fprintf(f, "%d,%d,%d\n", p, t, cnt);
		}
	}
	fclose(f);
	fprintf(stderr, "[figs] wrote fig5_alloc_dist.csv\n");
}

// Fig 7b: fraction of T0-allocated pages that are demoted in the next period
static void dump_fig7b_t0_demo(int nr_period) {
	FILE *f = fig_open("fig7b_t0_demo_ratio.csv", "period,n_alloc_t0,n_demoted_next");
	if (!f) return;
	for (int p = 0; p < nr_period - 1; p++) {
		auto it = analysis.alloc_addr_by_period[p].find(0);
		if (it == analysis.alloc_addr_by_period[p].end()) {
			fprintf(f, "%d,0,0\n", p);
			continue;
		}
		int n_alloc = (int)it->second.size();
		int n_demoted = 0;
		for (auto addr : it->second) {
			// still in T0 at p+1?
			if (analysis.cache_sched[p+1][0].count(addr) == 0) {
				// If it's in any lower tier at p+1 → demoted.
				for (int t = 1; t < analysis.nr_tiers; t++) {
					if (analysis.cache_sched[p+1][t].count(addr) > 0) { n_demoted++; break; }
				}
			}
		}
		fprintf(f, "%d,%d,%d\n", p, n_alloc, n_demoted);
	}
	fclose(f);
	fprintf(stderr, "[figs] wrote fig7b_t0_demo_ratio.csv\n");
}

// Fig 6: per-demotion record — src→dst with age (alloc_dist) and remain access count
static void dump_fig6_demo(int nr_period) {
	FILE *f = fig_open("fig6_demo.csv",
		"period,src_tier,dst_tier,addr,age_at_demo,remain_access");
	if (!f) return;
	for (int p = 0; p < nr_period; p++) {
		for (auto &entry : analysis.demo_addr_by_period[p]) {
			int src = entry.first.first;
			int dst = entry.first.second;
			for (auto addr : entry.second) {
				// Look up this addr's panal in period p (under I_TYPE_DEMO)
				struct page_anal *panal = nullptr;
				auto pit = analysis.total_page_anal[p][analysis.nr_tiers][I_TYPE_DEMO].find(addr);
				if (pit != analysis.total_page_anal[p][analysis.nr_tiers][I_TYPE_DEMO].end()) {
					panal = pit->second;
				}
				int age = panal ? panal->alloc_dist : -1;
				int remain = panal ? panal->future_freq_inclusive : -1;
				fprintf(f, "%d,%d,%d,%lu,%d,%d\n", p, src, dst, addr, age, remain);
			}
		}
	}
	fclose(f);
	fprintf(stderr, "[figs] wrote fig6_demo.csv\n");
}

// Fig 7a: per-page CDF data — number of accesses in the first 100 memory refs after the page's first access
static void dump_fig7a_burst() {
	FILE *f = fig_open("fig7a_burst_cdf.csv", "addr,alloc_time,n_access_in_next_100");
	if (!f) return;
	const int W = 100;
	for (auto &entry : analysis.access_per_addr) {
		uint64_t addr = entry.first;
		auto &v = entry.second;
		if (v.empty()) continue;
		int t0 = v[0]->access_time;
		int cnt = 0;
		for (auto *node : v) {
			if (node->access_time <= t0 + W) cnt++;
			else break;  // access vectors are in time order
		}
		fprintf(f, "%lu,%d,%d\n", addr, t0, cnt);
	}
	fclose(f);
	fprintf(stderr, "[figs] wrote fig7a_burst_cdf.csv\n");
}

// Fig 9: per-(period, page, target_tier) scoring — recency / freq / benefit / gain + MigOpt's choice
static void dump_fig9_scores(int nr_period) {
	FILE *f = fig_open("fig9_scores.csv",
		"period,addr,cur_tier,target_tier,recency,freq,benefit,cost,gain_a2,gain_a4,gain_a8,migopt_next_tier,migopt_promoted_here");
	if (!f) return;
	for (int p = 0; p < nr_period - 1; p++) {
		for (auto &addr_panal : analysis.total_page_anal[p][analysis.nr_tiers][I_TYPE_TOTAL]) {
			uint64_t addr = addr_panal.first;
			struct page_anal *panal = addr_panal.second;
			if (panal->cur_layer == 0) continue;  // can't promote from T0

			int recency = panal->last_access_time_dist;
			int freq    = panal->freq;

			// Look up MigOpt's decision for next period (if page still exists)
			int migopt_next = -1;
			auto it = analysis.total_page_anal[p+1][analysis.nr_tiers][I_TYPE_TOTAL].find(addr);
			if (it != analysis.total_page_anal[p+1][analysis.nr_tiers][I_TYPE_TOTAL].end()) {
				migopt_next = it->second->cur_layer;
			}

			// For each possible promotion target tier (faster than current)
			for (int tgt = 0; tgt < panal->cur_layer; tgt++) {
				int benefit = freq * (analysis.lat_loads[panal->cur_layer] - analysis.lat_loads[tgt]);
				int cost    = analysis.lat_4KB_reads[panal->cur_layer] + analysis.lat_4KB_writes[tgt];
				int gain_a2 = benefit - 2 * cost;
				int gain_a4 = benefit - 4 * cost;
				int gain_a8 = benefit - 8 * cost;
				int promoted_here = (migopt_next == tgt) ? 1 : 0;
				fprintf(f, "%d,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
					p, addr, panal->cur_layer, tgt,
					recency, freq, benefit, cost, gain_a2, gain_a4, gain_a8,
					migopt_next, promoted_here);
			}
		}
	}
	fclose(f);
	fprintf(stderr, "[figs] wrote fig9_scores.csv\n");
}

static void analysis_read_sched_file(const char *sched_file, bool detailed) {
	ifstream input_file(sched_file);
	if (!input_file.is_open()) {
		cerr << "HERE::: Error: Cannot open file: " << sched_file << endl;
		exit(1);
	}
	string str, type;
	uint64_t period, addr;
	int i_layer, i_type, prev_i_layer, prev_i_type;

	int nr_period = (analysis.nr_traces % analysis.period == 0) ? analysis.nr_traces / analysis.period : analysis.nr_traces / analysis.period + 1;
	int nr_items = analysis.total_accesses.size();
	int period_idx, prev_period_idx;
	int prev_level;

	analysis.cache_sched = vector<vector<unordered_map<uint64_t,vector<struct anode*>>>>(nr_period, vector<unordered_map<uint64_t,vector<struct anode*>>>(analysis.nr_tiers, unordered_map<uint64_t,vector<struct anode *>>()));

	analysis.demo_addr_by_period = vector<map<pair<int,int>,unordered_set<uint64_t>>>(nr_period, map<pair<int,int>,unordered_set<uint64_t>>());
	analysis.promo_addr_by_period = vector<map<pair<int,int>,unordered_set<uint64_t>>>(nr_period, map<pair<int,int>,unordered_set<uint64_t>>());
	analysis.alloc_addr_by_period = vector<map<int,unordered_set<uint64_t>>>(nr_period, map<int,unordered_set<uint64_t>>());
	analysis.remain_addr_by_period = vector<map<int,unordered_set<uint64_t>>>(nr_period, map<int,unordered_set<uint64_t>>());


	analysis.total_page_anal= vector<vector<vector<unordered_map<uint64_t,struct page_anal *>>>>(nr_period, vector<vector<unordered_map<uint64_t,struct page_anal *>>>(analysis.nr_tiers + 1, vector<unordered_map<uint64_t,struct page_anal *>>(NR_T, unordered_map<uint64_t, struct page_anal *>())));
	struct page_anal *panal;

	while(getline(input_file, str)) {
		stringstream ss(str);
		ss >> type >> period >> addr >> i_layer >> i_type;
		period_idx = period / analysis.period;
		if (analysis.item_sched.count(addr) == 0) {
			init_item_sched(addr, nr_period);
		}
		analysis.item_sched[addr][period_idx].layer = i_layer;
		analysis.item_sched[addr][period_idx].type = i_type;
		analysis.cache_sched[period_idx][i_layer].insert({addr,vector<struct anode *>()});

		if (period_idx == 0) { // new alloc
			if (analysis.alloc_addr_by_period[period_idx].count(i_layer) == 0)
				analysis.alloc_addr_by_period[period_idx].insert({i_layer, unordered_set<uint64_t>()});
			analysis.alloc_addr_by_period[period_idx][i_layer].insert(addr);

			panal = (struct page_anal *)malloc(sizeof(struct page_anal));
			memset(panal, 0, sizeof(struct page_anal));
			panal->addr = addr;
			panal->cur_layer = i_layer;
			panal->prev_layer = -1;
			panal->expected_layer = -1;
			panal->type = I_TYPE_ALLOC;
			panal->last_access_time = -1;
			panal->next_action = {-1,-1};

			analysis.total_page_anal[period_idx][analysis.nr_tiers][I_TYPE_TOTAL].insert({addr, panal});
			analysis.total_page_anal[period_idx][analysis.nr_tiers][I_TYPE_ALLOC].insert({addr, panal});
			analysis.total_page_anal[period_idx][i_layer][I_TYPE_TOTAL].insert({addr, panal});
			analysis.total_page_anal[period_idx][i_layer][I_TYPE_ALLOC].insert({addr, panal});

		} else {
			int prev_i_layer, prev_i_type;
			prev_i_layer = analysis.item_sched[addr][period_idx-1].layer;

			panal = (struct page_anal *)malloc(sizeof(struct page_anal));
			memset(panal, 0, sizeof(struct page_anal));
			panal->addr = addr;
			panal->cur_layer = i_layer;
			panal->expected_layer = -1;
			panal->prev_layer = prev_i_layer;
			panal->last_access_time = -1;
			panal->next_action = {-1,-1};

			analysis.total_page_anal[period_idx][analysis.nr_tiers][I_TYPE_TOTAL].insert({addr, panal});
			analysis.total_page_anal[period_idx][i_layer][I_TYPE_TOTAL].insert({addr, panal});

			if (prev_i_layer != -1)
			analysis.total_page_anal[period_idx-1][analysis.nr_tiers][I_TYPE_TOTAL][addr]->next_action = {prev_i_layer, i_layer};


			if (prev_i_layer == -1) { // new alloc
				if (analysis.alloc_addr_by_period[period_idx].count(i_layer) == 0)
					analysis.alloc_addr_by_period[period_idx].insert({i_layer, unordered_set<uint64_t>()});
				analysis.alloc_addr_by_period[period_idx][i_layer].insert(addr);

				panal->type = I_TYPE_ALLOC;
			} else if (prev_i_layer == i_layer) { // remains
				if (analysis.remain_addr_by_period[period_idx].count(i_layer) == 0)
					analysis.remain_addr_by_period[period_idx].insert({i_layer, unordered_set<uint64_t>()});
				analysis.remain_addr_by_period[period_idx][i_layer].insert(addr);

				panal->type = I_TYPE_REMAIN;
			} else if (prev_i_layer < i_layer) { // demo
				pair<int,int> key = {prev_i_layer, i_layer};
				if (analysis.demo_addr_by_period[period_idx].count(key) == 0)
					analysis.demo_addr_by_period[period_idx].insert({key, unordered_set<uint64_t>()});
				analysis.demo_addr_by_period[period_idx][key].insert(addr);

				panal->type = I_TYPE_DEMO;
			} else { // promo
				pair<int,int> key = {prev_i_layer, i_layer};
				if (analysis.promo_addr_by_period[period_idx].count(key) == 0)
					analysis.promo_addr_by_period[period_idx].insert({key, unordered_set<uint64_t>()});
				analysis.promo_addr_by_period[period_idx][key].insert(addr);

				panal->type = I_TYPE_PROMO;
			}
			analysis.total_page_anal[period_idx][analysis.nr_tiers][panal->type].insert({addr, panal});
			analysis.total_page_anal[period_idx][i_layer][panal->type].insert({addr, panal});
		}



	}

	struct anode *cur;
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		period_idx = (i-1) / analysis.period;
		addr = cur->addr;
		cur->period_idx = period_idx;

		if (analysis.item_sched[addr][period_idx].layer == -1) abort();

		i_layer = analysis.item_sched[addr][period_idx].layer;
		i_type = analysis.item_sched[addr][period_idx].type;

		cur->layer = i_layer;
		cur->node_type = i_type;
		if (period_idx > 0) {
			prev_i_layer = analysis.item_sched[addr][period_idx - 1].layer;
			prev_i_type = analysis.item_sched[addr][period_idx - 1].type;
			if (prev_i_layer == -1) { // ALLOC
				analysis.item_sched[addr][period_idx].type = I_TYPE_ALLOC;
			} else if (i_layer == prev_i_layer) { // HIT
				analysis.item_sched[addr][period_idx].type = I_TYPE_REMAIN;
			} else if (i_layer < prev_i_layer) { // PROMO
				analysis.item_sched[addr][period_idx].type = I_TYPE_PROMO;
				if(analysis.promo_addr.count(addr) == 0)
					analysis.promo_addr.insert({addr, vector<int>()});
				analysis.promo_addr[addr].push_back(i);
			} else { // DEMO
				analysis.item_sched[addr][period_idx].type = I_TYPE_DEMO;
				if(analysis.demo_addr.count(addr) == 0)
					analysis.demo_addr.insert({addr, vector<int>()});
				analysis.demo_addr[addr].push_back(i);
			}
			cur->prev_layer = prev_i_layer;
		} else {
			analysis.item_sched[addr][period_idx].type = I_TYPE_ALLOC;
			cur->prev_layer = -1;
		}
		cur->node_type = analysis.item_sched[addr][period_idx].type;


		if (analysis.cache_sched[period_idx][i_layer].count(addr) == 0)
			abort();
		analysis.cache_sched[period_idx][i_layer][addr].push_back(cur);
		if (analysis.access_per_addr.count(addr) == 0) {
			analysis.access_per_addr.insert({addr, vector<struct anode *>()});
		}
		analysis.access_per_addr[addr].push_back(cur);
	}



	if (detailed) {
	// Required preprocessing: populate panal fields (alloc_dist, future_freq_*, hotness, last_access_time_dist)
	post_proc_panal(nr_period);

	dump_fig5_alloc(nr_period);
	dump_fig7b_t0_demo(nr_period);
	dump_fig6_demo(nr_period);
	dump_fig7a_burst();
	dump_fig9_scores(nr_period);
	} // end if (detailed)

	unordered_map<uint64_t, pair<int,double>> freq[3];
	unordered_map<uint64_t, pair<int,double>> reuse[3];
	unordered_map<uint64_t, pair<int,double>> inter_ref[3];

	struct comp {
		bool operator()(pair<double,uint64_t>&a, pair<double,uint64_t>&b) {
			return a.first > b.first;
		}
	};

	priority_queue<pair<double,uint64_t>> freq_rank;
	priority_queue<pair<double,uint64_t>, vector<pair<double,uint64_t>>, comp> reuse_rank;
	priority_queue<pair<double,uint64_t>, vector<pair<double,uint64_t>>, comp> inter_ref_rank;
	std::list<uint64_t> base_lru;

	vector<vector<unordered_map<uint64_t,int>>> result_cache_sched (NR_COMP, vector<unordered_map<uint64_t,int>>(nr_period, unordered_map<uint64_t,int>()));

	unordered_set<uint64_t> g_first;

	vector<int> nr_new_allocs (nr_period, 0); 

	for (int i = 0; i < nr_period; i++) {
		for (int layer = 0; layer < analysis.nr_tiers; layer++) {
			for (auto item : analysis.cache_sched[i][layer]) {
				if (g_first.count(item.first) == 0) {
					nr_new_allocs[i]++;
					g_first.insert(item.first);
				}
			}
		}
	}
	g_first.clear();

	if (detailed) {
	printf("Tier size\n");
	for (int layer = 0; layer < analysis.nr_tiers; layer++) {
		for (int i = 0; i < nr_period; i++) {
			printf("%ld ", analysis.cache_sched[i][layer].size());
		}
		printf("\n");
	}
	}

	int nr_new_alloc = 0;
	int cur_level = 0;
	int rank = 0;

	for (int i = 0; i < nr_period; i++) {
		period = i * analysis.period;
		for (int layer = 0; layer < analysis.nr_tiers; layer++) {
			for (auto item : analysis.cache_sched[i][layer]) {
				if (g_first.count(item.first) == 0) {
					for (int j = 1; j < NR_COMP; j++) {
						result_cache_sched[j][i].insert({item.first,0});
					}
					g_first.insert(item.first);
					nr_new_alloc++;
				}
				result_cache_sched[0][i].insert({item.first,layer});

				for (int j = 0; j < item.second.size(); j++) {
					cur = item.second[j];
					addr = cur->addr;

					for (int k = 0; k < 3; k++) {
						if (freq[k].find(addr) == freq[k].end())
							freq[k][addr] = {0,0};
						if (reuse[k].find(addr) == reuse[k].end())
							reuse[k][addr] = {0,0};
						if (inter_ref[k].find(addr) == inter_ref[k].end())
							inter_ref[k][addr] = {0,0};

						freq[k][addr].first++;
						freq[k][addr].second++;

						if (reuse[k][addr].second == INT_MAX) {
							if (inter_ref[k][addr].second != INT_MAX)
								abort();
							reuse[k][addr].second = cur->reuse_dist;
							inter_ref[k][addr].second = cur->inter_ref;

						} else {
							reuse[k][addr].first++;
							reuse[k][addr].second += cur->reuse_dist;

							inter_ref[k][addr].first++;
							inter_ref[k][addr].second += cur->inter_ref;
						}
					}
				}
			}
		}

		for (int j = period + 1; j <= analysis.nr_traces && j <= period + analysis.period; j++) {
			cur = analysis.nodes + j;
			auto lru_it = find(base_lru.begin(), base_lru.end(), cur->addr);
			if (lru_it != base_lru.end()) {
				base_lru.erase(lru_it);
			}
			base_lru.push_front(cur->addr);
		}
	}

	vector<uint64_t> lats(NR_COMP, 0);
	vector<vector<uint64_t>> accs(NR_COMP, vector<uint64_t>(analysis.nr_tiers, 0));
	uint64_t base_lat = analysis.total_accesses.size() * analysis.lat_4KB_writes[analysis.nr_tiers - 1];
	uint64_t base_acc = 0;
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		period_idx = (i-1) / analysis.period;

		for (int j = 0; j < NR_COMP; j++) {
			cur_level = result_cache_sched[j][period_idx][cur->addr];
			if (cur_level == -1)
				abort();
			lats[j] += cur->type == LOAD ? analysis.lat_loads[cur_level] : analysis.lat_stores[cur_level];
			accs[j][cur_level]++;

		}

		base_lat += cur->type == LOAD ? analysis.lat_loads[analysis.nr_tiers - 1] : analysis.lat_stores[analysis.nr_tiers - 1];
		base_acc++;
	}


	vector<vector<vector<uint64_t>>> mig_cnt(NR_COMP, vector<vector<uint64_t>>(analysis.nr_tiers, vector<uint64_t>(analysis.nr_tiers, 0)));
	vector<uint64_t> mig_lats(NR_COMP, 0);
	vector<vector<uint64_t>> alloc_cnt(NR_COMP, vector<uint64_t>(analysis.nr_tiers, 0));
	vector<uint64_t> alloc_lats(NR_COMP, 0);
	for (int i = 0; i < NR_COMP; i++) {
		for (int j = 0; j < nr_period; j++) {
			for (auto item : result_cache_sched[i][j]) {
				if (j == 0) {
					alloc_lats[i] += analysis.lat_4KB_writes[item.second];
					alloc_cnt[i][item.second]++;
					continue;
				}

				if (result_cache_sched[i][j - 1].count(item.first)) {
					cur_level = item.second;
					prev_level = result_cache_sched[i][j - 1][item.first];
					if (prev_level != cur_level) {
						if (i == 0) { // migopt
							if (prev_level > cur_level) {
								analysis.promo_addr_set.insert(item.first);
							} else {
								analysis.demo_addr_set.insert(item.first);
							}

						}
						mig_cnt[i][prev_level][cur_level]++;
						mig_lats[i] += analysis.lat_4KB_reads[prev_level] + analysis.lat_4KB_writes[cur_level];
					}
				} else {
					alloc_lats[i] += analysis.lat_4KB_writes[item.second];
					alloc_cnt[i][item.second]++;

				}
			}
		}
	}

	printf("\n========================= BASIC REPORT =========================\n");
	printf("  sched file             : %s\n", sched_file);
	printf("  unique pages accessed  : %lu\n", analysis.access_per_addr.size());
	printf("  sampled traces (L+S)   : %lu\n", base_acc);
	printf("  number of tiers        : %d\n", analysis.nr_tiers);
	printf("  migration period       : %d accesses\n", analysis.period);
	printf("  migration traffic cap  : %d pages/period\n", analysis.mig_traffic);
	fflush(stdout);
	double type_throughput, base_throughput = (double) base_acc * 1000 * 1000 * 1000/ base_lat + analysis.total_accesses.size() * analysis.lat_4KB_writes[analysis.nr_tiers - 1];
	(void)type_throughput; (void)base_throughput;  // not used in basic report
	for (int i = 0; i < NR_COMP; i++) {
		uint64_t total_lat = lats[i] + alloc_lats[i] + mig_lats[i];
		double avg_acc   = base_acc > 0 ? (double)lats[i]   / (double)base_acc : 0.0;
		double avg_total = base_acc > 0 ? (double)total_lat / (double)base_acc : 0.0;

		printf("\n--- Variant %d ---\n", i);
		printf("  Latency totals (ns):\n");
		printf("    access      = %15lu\n", (uint64_t)lats[i]);
		printf("    allocation  = %15lu\n", (uint64_t)alloc_lats[i]);
		printf("    migration   = %15lu\n", (uint64_t)mig_lats[i]);
		printf("    TOTAL       = %15lu\n", (uint64_t)total_lat);
		printf("  Per-access averages (ns/access):\n");
		printf("    avg access  = %10.2f\n", avg_acc);
		printf("    avg TOTAL   = %10.2f  (access + amortized mig + alloc)\n", avg_total);

		// Accesses per tier
		uint64_t acc_sum = 0;
		for (int j = 0; j < analysis.nr_tiers; j++) acc_sum += accs[i][j];
		printf("  Accesses per tier:\n");
		printf("          ");
		for (int j = 0; j < analysis.nr_tiers; j++) printf("   T%-6d", j);
		printf("  | sum\n");
		printf("    cnt :");
		for (int j = 0; j < analysis.nr_tiers; j++) printf(" %9lu", accs[i][j]);
		printf(" | %lu\n", acc_sum);
		printf("    frac:");
		for (int j = 0; j < analysis.nr_tiers; j++)
			printf(" %9.4f", acc_sum > 0 ? (double)accs[i][j] / (double)acc_sum : 0.0);
		printf("\n");

		// Allocations per tier
		uint64_t alloc_sum = 0;
		for (int j = 0; j < analysis.nr_tiers; j++) alloc_sum += alloc_cnt[i][j];
		printf("  Allocations per tier:\n");
		printf("          ");
		for (int j = 0; j < analysis.nr_tiers; j++) printf("   T%-6d", j);
		printf("  | sum\n");
		printf("    cnt :");
		for (int j = 0; j < analysis.nr_tiers; j++) printf(" %9lu", alloc_cnt[i][j]);
		printf(" | %lu\n", alloc_sum);

		// Migrations src->dst
		uint64_t mig_total_cnt = 0;
		for (int j = 0; j < analysis.nr_tiers; j++)
			for (int k = 0; k < analysis.nr_tiers; k++)
				mig_total_cnt += mig_cnt[i][j][k];
		printf("  Migrations (src -> dst) [total=%lu]:\n", mig_total_cnt);
		printf("          ");
		for (int k = 0; k < analysis.nr_tiers; k++) printf("  ->T%-5d", k);
		printf(" | rowsum\n");
		for (int j = 0; j < analysis.nr_tiers; j++) {
			printf("    T%d  ->", j);
			uint64_t row = 0;
			for (int k = 0; k < analysis.nr_tiers; k++) {
				printf(" %8lu", mig_cnt[i][j][k]);
				row += mig_cnt[i][j][k];
			}
			printf(" | %lu\n", row);
		}
	}
	printf("==============================================================\n\n");
	fflush(stdout);
}

void analysis_get_performance() {
	unordered_map<uint64_t,int> mapping;

	struct anode *cur;
	uint64_t addr;
	int period_idx;

	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		period_idx = (i-1) / analysis.period;
		addr = cur->addr;
	}


}

static void analysis_migopt() {
	map<int, pair<int,double>> reuse_cdf, inter_ref_cdf, alloc_dist_cdf;
	int reuse, period, cur_level, inter_ref, alloc_dist;
	struct anode *cur;
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		period = (i-1) / analysis.period;
		reuse = cur->reuse_dist;
		inter_ref = cur->inter_ref;
		alloc_dist = cur->alloc_dist;

		if (reuse_cdf.count(reuse) == 0)
			reuse_cdf[reuse] = {0, 0};
		if (inter_ref_cdf.count(inter_ref) == 0)
			inter_ref_cdf[inter_ref] = {0, 0};
		if (alloc_dist_cdf.count(alloc_dist) == 0)
			alloc_dist_cdf[alloc_dist] = {0, 0};

		reuse_cdf[reuse].first++;
		inter_ref_cdf[inter_ref].first++;
		alloc_dist_cdf[alloc_dist].first++;
		cur_level = analysis.item_sched[cur->addr][period].layer;
		if (cur_level < 0) {
			abort();
		}
		reuse_cdf[reuse].second += cur_level;
		inter_ref_cdf[inter_ref].second += cur_level;
		alloc_dist_cdf[alloc_dist].second += cur_level;
	}

	map<uint64_t, pair<int,double>> item_avg_level;
	for (auto item : analysis.item_sched) {
		item_avg_level[item.first] = {0,0};
		for (int i = 0; i < item.second.size(); i++) {
			if (item.second[i].layer != -1) {
				item_avg_level[item.first].first++;
				item_avg_level[item.first].second += item.second[i].layer;
			}
		}
		item_avg_level[item.first].second /= item_avg_level[item.first].first;
	}

	//i-th
	map<int,pair<int,double>> ith_cdf;
	multimap<int,double,greater<int>> ith_total;
	int idx = 1;
	for (auto item : analysis.total_accesses) {
		if (ith_cdf.count(item.second.size()) == 0)
			ith_cdf[item.second.size()] = {0,0};

		ith_cdf[item.second.size()].first++;
		ith_cdf[item.second.size()].second += item_avg_level[item.first].second;
		ith_total.insert({item.second.size(), item_avg_level[item.first].second});
	}

	printf("Reuse cdf [reuse distance, no. of items, avg level, %%]\n");
	int sum = 0;
	for (auto item : reuse_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}

	printf("inter-ref cdf [ref distance, no. of items, avg level, %%]\n");
	sum = 0;
	for (auto item : inter_ref_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}

	printf("Alloc dist cdf [alloc distance, no. of items, avg level, %%]\n");
	sum = 0;
	for (auto item : alloc_dist_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}


	/*
	printf("i-th cdf [idx, Frequency, no. of items, avg level, %%]\n");
	idx = 0;
	sum = 0;
	for (auto item : ith_cdf) {
		sum += item.second.first;
		printf("%d %d %d %.2f %.2f\n", ++idx, item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_items);
	}
	*/


	printf("i-th total [idx, Frequency, avg level]\n");
	idx = 0;
	for (auto item : ith_total) {
		printf("%d %d %.2f\n", ++idx, item.first, item.second);
	}

	int cur_cnt = 0;
	int cur_pages = 0;
	int period_idx;
	int cur_type;
	int tot_cnt;
	printf("Total inter ref\n");
	for (auto item : analysis.total_accesses) {
		tot_cnt = item.second.size();

		if (tot_cnt <= 1) continue;

		cur_pages++;
		cur_cnt += tot_cnt;

		if (analysis.promo_addr.count(item.first)) printf("%d ", 1);
		else if (analysis.promo_addr.count(item.first)) printf("%d ", 2);
		else printf("%d ", 0);

		for (int i = 1; i < tot_cnt; i++) {
			cur = analysis.nodes + item.second[i];
			period_idx = (i-1) / analysis.period;
			cur_type = analysis.item_sched[cur->addr][period_idx].type;

			printf("%d ", cur->inter_ref);
		}
		printf("\n");
	}
	printf("Pages: %d, Accesses: %d\n", cur_pages, cur_cnt);

	printf("Promo inter ref\n");
	cur_pages = cur_cnt = 0;
	for (auto item : analysis.total_accesses) {
		tot_cnt = item.second.size();

		if (tot_cnt <= 1) continue;
		if (analysis.promo_addr_set.count(item.first) == 0) continue;

		cur_pages++;
		cur_cnt += tot_cnt;

		for (int i = 1; i < tot_cnt; i++) {
			cur = analysis.nodes + item.second[i];
			period_idx = (i-1) / analysis.period;
			cur_type = analysis.item_sched[cur->addr][period_idx].type;

			printf("%d ", cur->inter_ref);
		}
		printf("\n");
	}
	printf("Pages: %d, Accesses: %d\n", cur_pages, cur_cnt);

	inter_ref_cdf.clear();
	reuse_cdf.clear();
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		if (analysis.promo_addr_set.count(cur->addr) == 0) continue;
		period = (i-1) / analysis.period;
		reuse = cur->reuse_dist;
		inter_ref = cur->inter_ref;

		if (reuse_cdf.count(reuse) == 0)
			reuse_cdf[reuse] = {0, 0};
		if (inter_ref_cdf.count(inter_ref) == 0)
			inter_ref_cdf[inter_ref] = {0, 0};

		reuse_cdf[reuse].first++;
		inter_ref_cdf[inter_ref].first++;
		cur_level = analysis.item_sched[cur->addr][period].layer;
		if (cur_level < 0) {
			abort();
		}
		reuse_cdf[reuse].second += cur_level;
		inter_ref_cdf[inter_ref].second += cur_level;
	}
	printf("Inter-ref cdf (PROMO) [Ref distance, no. of items, avg level, %%]\n");
	sum = 0;
	for (auto item : inter_ref_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}

	printf("Demo inter ref\n");
	cur_pages = cur_cnt = 0;
	for (auto item : analysis.total_accesses) {
		tot_cnt = item.second.size();

		if (tot_cnt <= 1) continue;
		if (analysis.demo_addr_set.count(item.first) == 0) continue;

		cur_pages++;
		cur_cnt += tot_cnt;

		for (int i = 1; i < tot_cnt; i++) {
			cur = analysis.nodes + item.second[i];
			period_idx = (i-1) / analysis.period;
			cur_type = analysis.item_sched[cur->addr][period_idx].type;

			printf("%d ", cur->inter_ref);
		}
		printf("\n");
	}
	printf("Pages: %d, Accesses: %d\n", cur_pages, cur_cnt);
	inter_ref_cdf.clear();
	reuse_cdf.clear();
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		if (analysis.demo_addr_set.count(cur->addr) == 0) continue;
		period = (i-1) / analysis.period;
		reuse = cur->reuse_dist;
		inter_ref = cur->inter_ref;

		if (reuse_cdf.count(reuse) == 0)
			reuse_cdf[reuse] = {0, 0};
		if (inter_ref_cdf.count(inter_ref) == 0)
			inter_ref_cdf[inter_ref] = {0, 0};

		reuse_cdf[reuse].first++;
		inter_ref_cdf[inter_ref].first++;
		cur_level = analysis.item_sched[cur->addr][period].layer;
		if (cur_level < 0) {
			abort();
		}
		reuse_cdf[reuse].second += cur_level;
		inter_ref_cdf[inter_ref].second += cur_level;
	}
	printf("Inter-ref cdf (DEMO) [Ref distance, no. of items, avg level, %%]\n");
	sum = 0;
	for (auto item : inter_ref_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}

	printf("No mig inter ref\n");
	cur_pages = cur_cnt = 0;
	for (auto item : analysis.total_accesses) {
		tot_cnt = item.second.size();

		if (tot_cnt <= 1) continue;
		if (analysis.promo_addr_set.count(item.first)) continue;
		if (analysis.demo_addr_set.count(item.first)) continue;

		cur_pages++;
		cur_cnt += tot_cnt;

		for (int i = 1; i < tot_cnt; i++) {
			cur = analysis.nodes + item.second[i];
			period_idx = (i-1) / analysis.period;
			cur_type = analysis.item_sched[cur->addr][period_idx].type;

			printf("%d ", cur->inter_ref);
		}
		printf("\n");
	}
	printf("Pages: %d, Accesses: %d\n", cur_pages, cur_cnt);

	inter_ref_cdf.clear();
	reuse_cdf.clear();
	for (int i = 1; i <= analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		if (analysis.promo_addr_set.count(cur->addr) == 1) continue;
		if (analysis.demo_addr_set.count(cur->addr) == 1) continue;
		period = (i-1) / analysis.period;
		reuse = cur->reuse_dist;
		inter_ref = cur->inter_ref;

		if (reuse_cdf.count(reuse) == 0)
			reuse_cdf[reuse] = {0, 0};
		if (inter_ref_cdf.count(inter_ref) == 0)
			inter_ref_cdf[inter_ref] = {0, 0};

		reuse_cdf[reuse].first++;
		inter_ref_cdf[inter_ref].first++;
		cur_level = analysis.item_sched[cur->addr][period].layer;
		if (cur_level < 0) {
			abort();
		}
		reuse_cdf[reuse].second += cur_level;
		inter_ref_cdf[inter_ref].second += cur_level;
	}
	printf("Inter-ref cdf (NOMIG) [Ref distance, no. of items, avg level, %%]\n");
	sum = 0;
	for (auto item : inter_ref_cdf) {
		sum += item.second.first;
		printf("%d %d %.2f %.2f\n", item.first, item.second.first, item.second.second / item.second.first, (double)sum/analysis.nr_traces);
	}
}

static void analysis_print_all_access() {
	struct anode *cur;
	const char *path = "./results/all_access.csv";
	ofstream writeFile(path);
	for (int i = 1; i < analysis.nr_traces; i++) {
		cur = analysis.nodes + i;
		writeFile << cur->addr << " " << cur->reuse_dist << " " << cur->inter_ref << " " << cur->ref_cnt << " " << cur->layer << endl;
	}
	writeFile.close();

}

void do_analysis(const char *sched_file, bool detailed) {
	printf("sched_file: %s (detailed=%d)\n", sched_file, detailed ? 1 : 0);
	fflush(stdout);
	analysis_read_sched_file(sched_file, detailed);
}
