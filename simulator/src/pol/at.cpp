#include <map>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <stack>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <pthread.h>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include "at.h"
using namespace std;

#define NR_REV_DEMO 5

static struct at *my_at;
static vector<struct trace_req> traces;

void at_add_trace(struct trace_req &t) {
	traces.push_back(t);	
}

void init_at(struct sim_cfg &scfg) {
	my_at = new struct at;
	memset(my_at, 0, sizeof(struct at));
	my_at->nr_tiers = scfg.nr_tiers;
	my_at->mig_period = scfg.mig_period;
	my_at->mig_traffic = scfg.mig_traffic == -1 ? 1000 : scfg.mig_traffic;
	my_at->mode = scfg.do_at;
	my_at->sched_file = scfg.sched_file;

	for(int i = 0; i < scfg.nr_tiers; i++) {
		my_at->tier_lat_loads[i] = scfg.tier_lat_loads[i];
		my_at->tier_lat_stores[i] = scfg.tier_lat_stores[i];
		my_at->tier_lat_4KB_reads[i] = scfg.tier_lat_4KB_reads[i];
		my_at->tier_lat_4KB_writes[i] = scfg.tier_lat_4KB_writes[i];
		my_at->tiers[i].cap = scfg.tier_cap[i];
		my_at->tiers[i].size = 0;
	}

	for (int i = 0; i < my_at->nr_tiers; i++) {
		my_at->tiers[i].lru_list = new std::list<struct at_page *>();
	}

	my_at->pt = map<uint64_t, struct at_page *>();
	my_at->lru_list = new std::list<struct at_page *>();
}

void destroy_at() {
	for (auto &page : my_at->pt) {
		delete(page.second);
	}
	my_at->pt.clear();

	for (int i = 0; i < my_at->nr_tiers; i++) {
		my_at->tiers[i].lru_list->clear();
		delete my_at->tiers[i].lru_list;
	}
	my_at->lru_list->clear();
	delete my_at->lru_list;

	delete my_at;
	my_at = NULL;

	traces.clear();
}

static void update_tier_meta(struct at_page *page, bool is_new) {
	if (!is_new) {
		my_at->tiers[page->tier].lru_list->erase(page->t_iter);
	}

	my_at->tiers[page->tier].lru_list->push_front({page});
	
	page->t_iter = my_at->tiers[page->tier].lru_list->begin();
}

static void update_global_meta(struct at_page *page, bool is_new) {
	if (!is_new) {
		my_at->lru_list->erase(page->g_iter);
	}

	my_at->lru_list->push_front({page});

	page->g_iter = my_at->lru_list->begin();
}

static struct at_page *alloc_page(uint64_t addr) {
	int cur_tier;
	for (int i = 0; i < my_at->nr_tiers; i++) {
		cur_tier = my_at->alloc_order[i];
		if (my_at->tiers[cur_tier].size < my_at->tiers[cur_tier].cap)
			break;
	}

	if (my_at->tiers[cur_tier].size >= my_at->tiers[cur_tier].cap) {
		printf("cannot alloc in %d\n", cur_tier);
		abort();
	}

	struct at_page *page = new struct at_page;
	page->addr = addr;
	page->freq = 0;
	page->tier = cur_tier;
	my_at->tiers[cur_tier].size++;
	my_at->nr_alloc[cur_tier]++;

	return page;
}

static struct at_page *get_page(uint64_t addr, bool &is_new) {
	auto it = my_at->pt.find(addr);
	struct at_page *page;
	if (it == my_at->pt.end()) {
		page = alloc_page(addr);
		my_at->pt.insert({addr, page});
		is_new = true;
	} else {
		page = it->second;
		is_new = false;
	}

	return page;
}

static void at_proc_req(struct trace_req *t) {
	bool is_new;
	struct at_page *page = get_page(t->addr, is_new);

	page->freq++;

	update_global_meta(page, is_new);
	update_tier_meta(page, is_new);

	if (t->type == LOAD) my_at->nr_loads[page->tier]++;
	else my_at->nr_stores[page->tier]++;
	my_at->nr_accesses[page->tier]++;

	t->tier = page->tier;
}

static int do_promo(list<struct at_page *> &promo_list) {
	int src, dst;
	int nr_promo = 0;
	struct at_page *page;

	for (auto cand = promo_list.begin(); cand != promo_list.end(); cand++) {
		page = *cand;
		src = page->tier;
		dst = 0;

		page->tier = dst;
		my_at->nr_mig[src][dst]++;
		my_at->tiers[src].lru_list->erase(page->t_iter);
		my_at->tiers[dst].lru_list->push_front({page});
		page->t_iter = my_at->tiers[dst].lru_list->begin();
		my_at->tiers[src].size--; my_at->tiers[dst].size++;
		nr_promo++;
	}

	return nr_promo;
}

static int do_demo(list<struct at_page *> &demo_list, bool rev=false) {
	int src, dst;
	int nr_demo = 0;
	struct at_page *page;

	for (auto cand = demo_list.begin(); cand != demo_list.end(); cand++) {
		page = *cand;
		src = page->tier;

		if (rev == true) {
			if (src == 0) dst = 2;
			else if (src == 1) dst = 3;
			else
				abort();
		} else {
			if (src != 0)
				abort();

			if (my_at->tiers[2].size < my_at->tiers[2].cap)
				dst = 2;
			else
				dst = 3;
		}

		page->tier = dst;
		my_at->nr_mig[src][dst]++;
		my_at->tiers[src].lru_list->erase(page->t_iter);
		my_at->tiers[dst].lru_list->push_back({page});
		page->t_iter = prev(my_at->tiers[dst].lru_list->end());
		my_at->tiers[src].size--; my_at->tiers[dst].size++;
		if (my_at->tiers[src].size < 0 || my_at->tiers[dst].size > my_at->tiers[dst].cap)
			abort();
		nr_demo++;
	}

	return nr_demo;
}

// Scan tiers except tier-0 for the promotion
// the size of promo_list is limited by the mig_traffic or NR_REV_DEMO
static list<struct at_page*> scan_meta_for_promo(vector<int> &nr_free, vector<int> &nr_cand) {
	vector<vector<vector<struct at_page *>>> promo_cand(my_at->nr_tiers, vector<vector<struct at_page*>>(my_at->nr_tiers, vector<struct at_page *>()));
	list<struct at_page *> promo_list;

	int nr_need_to_move = 0;

	struct at_page *page;

	for (auto cand = my_at->lru_list->begin(); cand != my_at->lru_list->end(); ++cand) {
		page = *cand;

		if (nr_need_to_move == my_at->mig_traffic || -nr_free[0] >= (int)ceil((float)my_at->tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (page->tier == 0)
			continue;

		promo_list.push_front(page);
		nr_cand[page->tier]++;
		nr_free[0]--;
		nr_free[page->tier]++;

		nr_need_to_move++;

		if (nr_need_to_move >= my_at->tiers[0].cap)
			break;
	}
	
	return promo_list;
}

// Scan tier-0 for the demotion
static list<struct at_page *> scan_meta_for_demo(int nr_demo) {
	list<struct at_page *> demo_list;

	struct at_page *page;

	if (!nr_demo)
		return demo_list;

	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend(); ++cand) {
		page = *cand;

		demo_list.push_front(page);

		if (--nr_demo == 0)
			break;
	}

	return demo_list;
}

// Scan tier-0 and tier-1 to select canditates for reserving free spaces
static list<struct at_page *> scan_meta_for_rev_demo(int nr_rev_rate, vector<int> &nr_free) {
	list<struct at_page *> demo_list;

	int nr_demo[2];

	for (int i = 0; i < 2; i++) {
		nr_demo[i] = (int)ceil((float)my_at->tiers[i].cap * nr_rev_rate/100);
		if (nr_free[i] < nr_demo[i])
			nr_demo[i] = nr_demo[i] - nr_free[i];
		else
			nr_demo[i] = 0;
	}

	struct at_page *page;

	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend() && nr_free[2] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[2]--;

		if (--nr_demo[0] == 0)
			break;
	}

	for (auto cand = my_at->tiers[1].lru_list->rbegin(); cand != my_at->tiers[1].lru_list->rend() && nr_free[3] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[3]--;

		if (--nr_demo[1] == 0)
			break;
	}

	return demo_list;
}

static vector<int> calc_nr_free() {
	vector<int> nr_free = vector<int>(my_at->nr_tiers, 0);

	for (int i = 0; i < my_at->nr_tiers; i++) {
		nr_free[i] = my_at->tiers[i].cap - my_at->tiers[i].size;
		if(nr_free[i] < 0)
			abort();
	}

	return nr_free;
}

// If the nr_free[0] is negative, it means that the tier-0 has to demote some pages
// to tier-2 or tier-3. The nr_free[2] and nr_free[3] are the free spaces in tier-2 and tier-3
// If nr_free[2] + nr_free[3] is not enough to accommodate the demoted pages from tier-0,
// cancle the pages of tier-1 in the promo_list.
static int adjust_promo_cand_for_demo(list<struct at_page*> &promo_list, vector<int> &nr_free) {
	int nr_should_demo = -nr_free[0];

	if (nr_should_demo <= 0)
		// no need to demote
		return 0;

	struct at_page *page;
	auto list_it = promo_list.begin();

	// if nr_free has not enough space for a demotion, cancel the promoted page
	while(nr_should_demo > (nr_free[2] + nr_free[3]) && list_it != promo_list.end()) {
		page = *list_it;

		if (page->tier == 1) { // cancel pages
			list_it = promo_list.erase(list_it);
			nr_should_demo--;
			nr_free[0]++;
			nr_free[page->tier]--;
		} else {
			list_it++;
		}
	}

	if (nr_should_demo > (nr_free[2] + nr_free[3])) {
		printf("Tier-2,3 full!\n");
		abort();
	}

	return nr_should_demo;
}

// If the promo_list has enough free space, refill the promo_list using the lru_list
// and the demo_list using the tier-0 lru_list.
// The refilled pages must be in tier-2 or tier-3.
static void refill_promo_demo_list(list<struct at_page *> &promo_list, list<struct at_page *> &demo_list) {
	if (promo_list.size() == my_at->mig_traffic)
		return;

	auto promo_begin = promo_list.begin();
	auto demo_begin = demo_list.begin();

	struct at_page *page;
	volatile bool start_select = false;
	

	for (auto cand = my_at->lru_list->begin(); cand != my_at->lru_list->end(); ++cand) {
		page = *cand;

		if (promo_list.size() >= my_at->mig_traffic || promo_list.size() >= my_at->tiers[0].cap ||
				promo_list.size() + my_at->tiers[0].size >= my_at->tiers[0].cap + (int)ceil((float)my_at->tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (start_select && (page->tier == 2 || page->tier == 3)) {
			promo_list.push_front(page);
		}

		if (page == *promo_begin)
			start_select = true;
	}

	start_select = false;
	int nr_demo = my_at->tiers[0].size + promo_list.size() - my_at->tiers[0].cap;

	if (nr_demo <= 0)
		return;

	if (nr_demo < demo_list.size())
		abort();


	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend(); ++cand) {
		page = *cand;

		if (demo_list.size() == nr_demo)
			break;

		if (start_select) {
			demo_list.push_front(page);
		}

		if (page == *demo_begin)
			start_select = true;
	}

	return;
}

static int do_mig() {
	vector<int> nr_free = calc_nr_free();
	vector<int> nr_promo_cand = vector<int>(my_at->nr_tiers, 0);

	auto promo_list = scan_meta_for_promo(nr_free, nr_promo_cand);

	if (promo_list.empty())
		return 0;

	int nr_demo = adjust_promo_cand_for_demo(promo_list, nr_free);

	auto demo_list = scan_meta_for_demo(nr_demo);

	calc_nr_free();

	refill_promo_demo_list(promo_list, demo_list);

	if (do_promo(promo_list) == 0)
		return 0;


	if (do_demo(demo_list) < 0)
		return -1;

	
	nr_free = calc_nr_free();

	demo_list = scan_meta_for_rev_demo(NR_REV_DEMO, nr_free);
	if (do_demo(demo_list, true) < 0)
		return -1;
	
	calc_nr_free();

	return 0;
}

static struct at_perf calc_perf() {
	int64_t tier_lat_acc = 0, tier_lat_mig = 0, tier_lat_alc = 0;
	for (int i = 0; i < my_at->nr_tiers; i++) {
		tier_lat_acc += my_at->nr_loads[i] * my_at->tier_lat_loads[i] + my_at->nr_stores[i] * my_at->tier_lat_stores[i];
		tier_lat_alc += my_at->nr_alloc[i] * my_at->tier_lat_4KB_writes[i];
	}

	for (int i = 0; i < my_at->nr_tiers; i++) {
		for (int j = 0; j < my_at->nr_tiers; j++) {
			tier_lat_mig += my_at->nr_mig[i][j] * (my_at->tier_lat_4KB_reads[i] + my_at->tier_lat_4KB_writes[j]);
		}
	}

	struct at_perf ret = {tier_lat_acc, tier_lat_mig, tier_lat_alc};

	return ret;
}

void *__do_at (vector<int> &alloc_order) {
	for (int i = 0; i < my_at->nr_tiers; i++) {
		my_at->alloc_order[i] = alloc_order[i];
	}

	for (int i = 0; i < traces.size(); i++) {
		at_proc_req(&traces[i]);
		if (i != 0 && (i % my_at->mig_period) == 0) {
			if (do_mig() < 0) {
				abort();
			}
		}
	}

	my_at->perf = calc_perf();

	return my_at;
}

static void clear_at() {
	for (int i = 0; i < my_at->nr_tiers; i++) {
		my_at->nr_alloc[i] = 0;
		my_at->nr_loads[i] = 0;
		my_at->nr_stores[i] = 0;
		my_at->nr_accesses[i] = 0;
		my_at->tiers[i].size = 0;
		my_at->tiers[i].lru_list->clear();
	}

	for (int i = 0; i < my_at->nr_tiers; i++) {
		for (int j = 0; j < my_at->nr_tiers; j++) {
			my_at->nr_mig[i][j] = 0;
		}
	}

	for (auto &page : my_at->pt) {
		delete(page.second);
	}

	my_at->pt.clear();
	my_at->lru_list->clear();
}

string print_at_sched () {
	// set sched file name
	string aorder = "";
	for (int i = 0; i < my_at->nr_tiers; i++)
		aorder += to_string(my_at->alloc_order[i]);

	string output_file = my_at->sched_file;
	output_file = output_file + ".at_mode" + to_string(my_at->mode) + ".aorder" + aorder + ".sched";

	ofstream writeFile(output_file.c_str());

	// generate migration schedule 
	int nr_period = (traces.size() % my_at->mig_period == 0) ? traces.size() / my_at->mig_period : traces.size() / my_at->mig_period + 1;
	vector<map<uint64_t,int>> mig_sched = vector(nr_period, map<uint64_t,int>());
	int period = 0;
	for (int i = 0; i < traces.size(); i++) {
		period = i / my_at->mig_period;

		if (i > 0 && (i-1)/my_at->mig_period != period) {
			for (auto &item : mig_sched[period-1]) {
				mig_sched[period].insert(item);
			}
		}
		
		mig_sched[period][traces[i].addr] = traces[i].tier;
	}

	for (int i = 0; i < nr_period; i++) {
		for (auto item: mig_sched[i]) {
				writeFile << "A " << i * my_at->mig_period << " " << item.first << " " << item.second << " " << 0 << "\n";
		}
	}

	fflush(stdout);
	writeFile.close();

	return output_file;
}

void print_at() {
	cout << "==========================\n";
	cout << "Printing AT stats\n";
	cout << "mode: " << my_at->mode << endl;
	cout << "alloc order: ";
	for (int i = 0; i < my_at->nr_tiers; i++) {
		cout << my_at->alloc_order[i] << " ";
	}
	cout << endl;

	cout << "lat_acc lat_mig lat_alc" << endl;
	cout << my_at->perf.lat_acc << " " << my_at->perf.lat_mig <<  " " << my_at->perf.lat_alc << endl; 

	cout << "alloc stat\n";
	for (int i = 0; i < my_at->nr_tiers; i++) {
		cout << my_at->nr_alloc[i] << " ";
	}
	cout << endl;

	cout << "access stat\n";
	for (int i = 0; i < my_at->nr_tiers; i++) {
		cout << my_at->nr_accesses[i] << " ";
	}
	cout << endl;

	cout << "mig traffic\n"; 
	for (int i = 0; i < my_at->nr_tiers; i++) {
		for (int j = 0; j < my_at->nr_tiers; j++) {
			cout << my_at->nr_mig[i][j] << " ";
		}
		cout << endl;
	}

	return;
}

string do_at() {
	string output_file;
	vector<vector<int>> alloc_orders = {{0,2,1,3}, {1,0,2,3}, {2,0,1,3}, {0,1,2,3}};

	for (int alloc_id = 0; alloc_id < alloc_orders.size(); alloc_id++) {
		 __do_at(alloc_orders[alloc_id]);

		print_at();
		auto str = print_at_sched();
		if (alloc_id == 0) {
			output_file = str;
		}
		clear_at();
	}

	return output_file;
}
