#include <iostream>
#include <random>
#include <limits>
#include <time.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <functional>
#include <list>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <unordered_set>
#include <libconfig.h>
#include "sim.h"
#include "an.h"
#include "at.h"
#include "mtm.h"
#include "migopt.h"
#include "analysis.h"
//#include "mcmf.h" // for testing
using namespace std;

enum trace_type get_type(string &str) {
	string sub_str = str.substr(0,10);
	if (sub_str[0] == 'R') {
		return LOAD;
	} else if (sub_str[0] == 'W') {
		return STORE;
	}
	return OTHERS;
}

static inline unsigned long long get_mem_addr(string str) {
	char *end;
	unsigned long long addr;
	string addr_str = str.substr(2, str.size());
	addr = strtoull(addr_str.c_str(), &end, 10);
	return addr;
}

static inline struct trace_req get_trace_req(string &str) {
	struct trace_req treq = {0};
	treq.addr = get_mem_addr(str);
	treq.type = get_type(str);
	treq.tier = -1;
	return treq;
}

/*
void print_stat () {
	// Use setw to format the output into aligned columns for "TOTAL", "LOAD", and "STORE".
	cout << setw(20) << "TOTAL" << setw(10)<< "LOAD" << setw(10) << "STORE" << endl;
	auto total_traces = tstat.nr_trace[LOAD] + tstat.nr_trace[STORE];
	cout << setw(20) << total_traces << setw(10) << tstat.nr_trace[LOAD] << setw(10) << tstat.nr_trace[STORE] << endl;

	double lat_total = calc_total_latency();
	double lat_load = calc_load_latency();
	double lat_store = calc_store_latency();
	double lat_alloc = calc_alloc_latency();
	double lat_promo = calc_promo_latency();
	double lat_demo = calc_demo_latency();
	auto mig = get_migration_pages();

	for (int i = 0; i < mig.size(); i++) {
		for (int j = 0; j < mig[i].size(); j++) {
			printf("%ld ", mig[i][j]);
		}
		printf("\n");
	}

	printf("Total latency (ns): %.2f\n", lat_total);
	printf("Total load latency (ns): %.2f\n", lat_load);
	printf("Total store latency (ns): %.2f\n", lat_store);
	printf("Total alloc latency (ns): %.2f\n", lat_alloc);
	printf("Total promo latency (ns): %.2f\n", lat_promo);
	printf("Total demo latency (ns): %.2f\n", lat_demo);

	lat_total = lat_total / 1000 / 1000 / 1000;
	double throughput = (tstat.nr_trace[LOAD] + tstat.nr_trace[STORE]) / lat_total;
	printf("Throughput (instructions per second): %.2f\n", throughput);
	lat_total = (lat_load + lat_store + lat_alloc) / 1000 / 1000 / 1000;
	throughput = (tstat.nr_trace[LOAD] + tstat.nr_trace[STORE]) / lat_total;
	printf("Throughput w/o migration (instructions per second): %.2f\n", throughput);
}
*/

void print_conf(struct sim_cfg &scfg) {
	// Print the configuration all settings in readable formats
	cout << "Configuration:" << endl;
	cout << "Trace File: " << scfg.trace_file << endl;
	cout << "Sampled File: " << scfg.sampled_file << endl;
	cout << "Sched File: " << scfg.sched_file << endl;
	cout << "Analysis Input File: " << scfg.analysis_input_file << endl;
	cout << "Number of Original Pages: " << scfg.nr_org_pages << endl;
	cout << "Number of Original Traces: " << scfg.nr_org_traces << endl;
	cout << "Trace Sampling Ratio: " << scfg.trace_sampling_ratio << endl;
	cout << "Number of Sampled Pages: " << scfg.nr_sampled_pages << endl;
	cout << "Number of Sampled Traces: " << scfg.nr_sampled_traces << endl;
	cout << "Number of Tiers: " << scfg.nr_tiers << endl;
	cout << "Tier Capacity Scale: " << scfg.tier_cap_scale << endl;
	cout << "Tier Capacity Ratio: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_cap_ratio[i] << " ";
	}
	cout << endl;
	cout << "Total Capacity: " << scfg.total_cap << endl;
	cout << "Tier Capacities: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_cap[i] << " ";
	}
	cout << endl;
	cout << "Tier Load Latencies: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_lat_loads[i] << " ";
	}
	cout << endl;
	cout << "Tier Store Latencies: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_lat_stores[i] << " ";
	}
	cout << endl;
	cout << "Tier 4KB Read Latencies: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_lat_4KB_reads[i] << " ";
	}
	cout << endl;
	cout << "Tier 4KB Write Latencies: ";
	for (int i = 0; i < scfg.nr_tiers; i++) {
		cout << scfg.tier_lat_4KB_writes[i] << " ";
	}
	cout << endl;
	cout << "Migration Period: " << scfg.mig_period << endl;
	cout << "Migration Traffic: " << scfg.mig_traffic << endl;
	cout << "Migration Overhead: " << scfg.mig_overhead << endl;
	cout << "Do AutoNUMA: " << scfg.do_an << endl;
	cout << "Do AutoTiering: " << scfg.do_at << endl;
	cout << "Do MTM: " << scfg.do_mtm << endl;
	cout << "Do MigOpt: " << scfg.do_migopt << endl;
	cout << "Do Analysis: " << scfg.do_analysis << endl;
}

void get_sim_conf(const char *cfg_file, struct sim_cfg &scfg) {
	config_t cfg;
	config_setting_t *setting;
	int intval;
	const char *str;
	long long int int64val;

	config_init(&cfg);
	/* Read the file. If there is an error, report it and exit. */
	if(!config_read_file(&cfg, cfg_file))
	{
		fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
				config_error_line(&cfg), config_error_text(&cfg));
		config_destroy(&cfg);
	}

	if(config_lookup_string(&cfg, "trace_file", &str))
		memcpy(scfg.trace_file, str, strlen(str));
	if (config_lookup_int(&cfg, "trace_sampling_ratio", &intval))
		scfg.trace_sampling_ratio = intval;
	if(config_lookup_string(&cfg, "analysis_input_file", &str))
		memcpy(scfg.analysis_input_file, str, strlen(str));

	string dot_removed_file_str = scfg.trace_file;
	// Remove the file extension (.) from the trace file name
	size_t last_dot = dot_removed_file_str.find_last_of(".");
	if (last_dot != string::npos) {
		dot_removed_file_str = dot_removed_file_str.substr(0, last_dot);
	}

	string after_slash_file_str = dot_removed_file_str.substr(dot_removed_file_str.find_last_of("/")+1);

	// Derive the sampled-trace and schedule file names. A trace_file that
	// already ends in ".trace" is used as-is (pre-sampled trace).
	string file_str_in = string(scfg.trace_file);
	string sampled_file_str, sched_file_str;
	if (file_str_in.size() > 6 &&
	    file_str_in.compare(file_str_in.size() - 6, 6, ".trace") == 0) {
		sampled_file_str = file_str_in;
		sched_file_str = "./results/" + after_slash_file_str;
	} else {
		sampled_file_str = dot_removed_file_str + ".ratio" + to_string(scfg.trace_sampling_ratio) + ".sampled";
		sched_file_str = "./results/" + after_slash_file_str + ".ratio" + to_string(scfg.trace_sampling_ratio);
	}
	printf("%s\n", sampled_file_str.c_str());
	// make sure the schedule output directory exists
	if (system("mkdir -p ./results") != 0)
		fprintf(stderr, "warning: could not create ./results\n");
	// copy the sampled file name and sched file name to the scfg
	memcpy(scfg.sampled_file, sampled_file_str.c_str(), sampled_file_str.length() + 1);
	memcpy(scfg.sched_file, sched_file_str.c_str(), sched_file_str.length() + 1);


	// get tier info (nr_tiers, tier_cap_scale, tier_cap_ratio, and latencies)
	if(config_lookup_int(&cfg, "nr_tiers", &intval))
		scfg.nr_tiers = intval;
	if(config_lookup_int(&cfg, "tier_cap_scale", &intval))
		scfg.tier_cap_scale = intval;
	setting = config_lookup(&cfg, "tier_cap_ratio");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("tier_cap_ratio's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *ratio = config_setting_get_elem(setting, i); 
			scfg.tier_cap_ratio[i] =  config_setting_get_int(ratio);
		}
	}

	// get mig_period, mig_traffic, and mig_overhead
	if(config_lookup_int(&cfg, "mig_period", &intval))
		scfg.mig_period = intval;
	if(config_lookup_int(&cfg, "mig_traffic", &intval))
		scfg.mig_traffic = intval;
	if(config_lookup_int(&cfg, "mig_overhead", &intval))
		scfg.mig_overhead = intval;

	setting = config_lookup(&cfg, "tier_lat_loads");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("tier_lat_loads's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.tier_lat_loads[i] =  config_setting_get_int(lat);
		}
	}
	setting = config_lookup(&cfg, "tier_lat_stores");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("tier_lat_stores's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.tier_lat_stores[i] =  config_setting_get_int(lat);
		}
	}
	setting = config_lookup(&cfg, "tier_lat_4KB_reads");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("tier_lat_4KB_reads's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.tier_lat_4KB_reads[i] = config_setting_get_int(lat);
			scfg.tier_lat_4KB_reads[i] = scfg.tier_lat_4KB_reads[i] * scfg.mig_overhead / 10000;
		}
	}
	setting = config_lookup(&cfg, "tier_lat_4KB_writes");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("tier_lat_4KB_writes's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.tier_lat_4KB_writes[i] = config_setting_get_int(lat);
			scfg.tier_lat_4KB_writes[i] = scfg.tier_lat_4KB_writes[i] * scfg.mig_overhead / 10000;
		}
	}

	// get simulating actions (e.g., do_an, do_at, do_mtm)
	if(config_lookup_int(&cfg, "do_an", &intval))
		scfg.do_an = intval;
	if(config_lookup_int(&cfg, "do_at", &intval))
		scfg.do_at = intval;
	if(config_lookup_int(&cfg, "do_mtm", &intval))
		scfg.do_mtm = intval;
	if(config_lookup_int(&cfg, "do_migopt", &intval))
		scfg.do_migopt = intval;
	if(config_lookup_int(&cfg, "do_analysis", &intval))
		scfg.do_analysis = intval;
}

static bool get_hash(uint64_t addr, uint64_t sample) {
	hash<uint64_t> hash_fn;
	if (sample == TRACE_SAMPLE)
		return true;
	size_t hash_value = hash_fn(addr);

	if ((hash_value % TRACE_SAMPLE) < sample) {
		return true;
	}

	return false;
}

static bool need_to_read_org_trace(struct sim_cfg &scfg) {
	// Check if the sampled file exists and is not empty
	// If it exists, we don't need to read the trace again
	ifstream infile(scfg.sampled_file);
	if (infile.good()) {
		infile.seekg(0, ios::end);
		if (infile.tellg() > 0) {
			return false; // File exists and is not empty
		}
	}

	// If the file doesn't exist or is empty, we need to read the trace
	return true;
}

static int read_sampled_trace(vector<struct trace_req> &traces, struct sim_cfg &scfg, struct sim_stat &sstat) {
	unordered_set<uint64_t> sampled_pages;
	int linen = 0;
	ifstream infile(scfg.sampled_file);
	if (!infile.is_open()) {
		cerr << "Error opening sampled trace file: " << scfg.sampled_file << endl;
		return -1;
	}

	string line;
	while (getline(infile, line)) {
		struct trace_req treq = get_trace_req(line);
		switch (treq.type) {
			case LOAD:
			case STORE:
				sampled_pages.insert(treq.addr);
				sstat.nr_sampled_traces[treq.type]++;
				sstat.nr_sampled_traces[TOTAL]++;
				traces.push_back(treq);
				break;
			default:
				break;
		}
		linen++;
	}

	scfg.nr_sampled_pages = sampled_pages.size();
	scfg.nr_sampled_traces = sstat.nr_sampled_traces[TOTAL];

	infile.close();
	return 0;
}

static int read_org_trace(vector<struct trace_req> &traces, struct sim_cfg &scfg, struct sim_stat &sstat) {
	unordered_set<uint64_t> org_pages, sampled_pages;
	struct trace_req treq = {0};
	uint64_t lines = 0;
	
	ifstream input_file(scfg.trace_file);
	if (!input_file.is_open()) {
		cerr << "Error opening trace file: " << scfg.trace_file << endl;
		return -1;
	}

	// Open the sampled trace file for writing
	ofstream sampled_file(scfg.sampled_file);
	if (!sampled_file.is_open()) {
		cerr << "Error opening sampled trace file: " << scfg.sampled_file << endl;
		input_file.close();
		return -1;
	}

	// Do sampling with trace sampling ratio
	// and get the number of original/sampled pages and the number of original/sampled traces
	// and write the sampled trace to the file
	string cur_str;
	while (getline(input_file, cur_str)) {
		if ((++lines % 2000) == 0){
			fprintf(stderr, "\r%lu processed...", lines);
		}

		treq = get_trace_req(cur_str);
		switch (treq.type) {
			case LOAD:
			case STORE:
				if (!get_hash(treq.addr, scfg.trace_sampling_ratio)) {
					org_pages.insert(treq.addr);
					sstat.nr_org_traces[treq.type]++;
					sstat.nr_org_traces[TOTAL]++;
				} else {
					sampled_pages.insert(treq.addr);
					sstat.nr_sampled_traces[treq.type]++;
					sstat.nr_sampled_traces[TOTAL]++;
					traces.push_back(treq);
					// Write the sampled trace to the file
					sampled_file << cur_str << endl;
				}
				break;
			default:
				break;
		}
	}

	scfg.nr_org_pages = org_pages.size();
	scfg.nr_org_traces = sstat.nr_sampled_traces[TOTAL];
	scfg.nr_sampled_pages = sampled_pages.size();
	scfg.nr_sampled_traces = sstat.nr_sampled_traces[TOTAL];

	input_file.clear();
	input_file.seekg(0);
	input_file.close();

	// flush the sampled trace file
	sampled_file.flush();
	sampled_file.close();

	return 0;
}

static void calc_tier_cap(struct sim_cfg &scfg) {
	// Set the capacity of each tier
	scfg.total_cap = scfg.nr_sampled_pages * (100 + scfg.tier_cap_scale) / 100;
	int total_cap_ratio = 0;
	for (int i = 0; i < scfg.nr_tiers; i++) {
		total_cap_ratio += scfg.tier_cap_ratio[i];
	}

	for (int i = 0; i < scfg.nr_tiers; i++) {
		scfg.tier_cap[i] = scfg.total_cap * scfg.tier_cap_ratio[i] / total_cap_ratio;
		if (scfg.tier_cap[i] <= 0) {
			cout << "Tier " << i << " capacity is less than 0!" << endl;
			abort();
		}
	}
}

static int read_trace(vector<struct trace_req> &traces, struct sim_cfg &scfg, struct sim_stat &sstat) {
	unordered_set<uint64_t> org_pages, sampled_pages;
	struct trace_req treq = {0};
	uint64_t lines = 0;
	int ret = 0;

	// Check if we need to read the trace file
	if (need_to_read_org_trace(scfg)) {
		// If the sampled trace file does not exist or is empty, we need to read the original trace file
		cout << "Reading original trace file: " << scfg.trace_file << endl;
		ret = read_org_trace(traces, scfg, sstat);
	} else {
		// If the sampled trace file already exists, we don't need to read the original trace file again
		// Just read the sampled trace file
		cout << "Sampled trace file already exists. No need to read the trace file again." << endl;
		ret = read_sampled_trace(traces, scfg, sstat);
	}

	return ret;
}

// Initialize the simulators
void init_sim(struct sim_cfg &scfg) {

	// Initialize the simulators
	if (scfg.do_an > 0)
		init_an(scfg);
	if (scfg.do_at > 0)
		init_at(scfg);
	if (scfg.do_mtm > 0)
		init_mtm(scfg);
	if (scfg.do_migopt > 0)
		init_migopt(scfg);
	// Analysis init is always needed (basic report always runs)
	init_analysis(scfg);
}

void process_trace(struct trace_req &trace, struct sim_cfg &scfg) {
	switch (trace.type) {
		case LOAD:
		case STORE:
			if (scfg.do_an > 0)
				an_add_trace(trace);
			if (scfg.do_at > 0)
				at_add_trace(trace);
			if (scfg.do_mtm > 0)
				mtm_add_trace(trace);
			if (scfg.do_migopt > 0)
				migopt_add_trace(trace);
			// Analysis trace accumulation is always needed (basic report always runs)
			analysis_add_trace(trace);
			break;
		default:
			break;
	}
}

// Do simulation
void do_sim(vector<struct trace_req> &traces, struct sim_cfg &scfg) {
	// Process each trace request
	for (auto &trace : traces) {
		process_trace(trace, scfg);
	}

	string output_file;

	// Basic report is printed unconditionally after each policy's sched is generated.
	// Detailed analysis (paper §4 stats) runs only when scfg.do_analysis > 0.
	bool detailed = (scfg.do_analysis > 0);

	if (scfg.do_an > 0) {
		output_file = do_an();
		do_analysis(output_file.c_str(), detailed);
	}

	if (scfg.do_at > 0) {
		output_file = do_at();
		do_analysis(output_file.c_str(), detailed);
	}

	if (scfg.do_mtm > 0) {
		output_file = do_mtm();
		do_analysis(output_file.c_str(), detailed);
	}

	if (scfg.do_migopt > 0) {
		output_file = do_migopt();
		do_analysis(output_file.c_str(), detailed);
	}

	if (scfg.analysis_input_file[0] != '\0') {
		// Standalone analysis from an existing sched file
		do_analysis(scfg.analysis_input_file, detailed);
	}
}

void destroy_sim(struct sim_cfg &scfg) {
	if (scfg.do_an > 0)
		destroy_an();
	if (scfg.do_at > 0)
		destroy_at();
	if (scfg.do_mtm > 0)
		destroy_mtm();
	if (scfg.do_migopt > 0)
		destroy_migopt();
	destroy_analysis();
}

void print_sim(struct sim_stat &sstat) {
	// Print the simulation statistics
	cout << "Simulation Statistics:" << endl;
	cout << "Original Traces: " << sstat.nr_org_traces[TOTAL] << endl;
	cout << "Sampled Traces: " << sstat.nr_sampled_traces[TOTAL] << endl;
}

int main(int argc, char **argv) {
	string cur_str;
	struct sim_cfg scfg = {0};
	struct sim_stat sstat = {0};
	const char *cfg_file = "configs/small-ycsb.cfg";

	memset(&sstat, 0, sizeof(sstat)); // Ensure tstat.nr_trace is properly initialized

	if (argc > 1)
		cfg_file = argv[1];

	get_sim_conf(cfg_file, scfg);

	vector<struct trace_req> sampled_traces;
	read_trace(sampled_traces, scfg, sstat);

	calc_tier_cap(scfg);

	print_conf(scfg);

	init_sim(scfg);

	do_sim(sampled_traces, scfg);

	destroy_sim(scfg);

	return 0;
}
