#ifndef SIMUL_H
#define SIMUL_H

#include <iostream>
#include <list>
#include <unordered_map>
#include <queue>
#include <array>
#include <climits>
#include <cstdio>
#include <algorithm>
#include <utility>

using namespace std;

class SimulSetup {
public:
    int *tier_cap;
    int *tier_load_lat;
    int *tier_4KB_R_lat;
    int *tier_4KB_W_lat;

    int kmig_period;          // migration-phase length (instructions)
    double auto_reclaim_low;  // AutoTiering reclaim trigger (T0 free ratio)

    int mig_quota;            // migrations allowed per phase
    int mig_budget;           // remaining budget in the current phase
    int instruction_num;
    int unique_va_num;
    int inst;                 // current instruction index

    SimulSetup() {
        tier_cap = new int[4];
        tier_load_lat = new int[4];
        tier_4KB_R_lat = new int[4];
        tier_4KB_W_lat = new int[4];
    }

    ~SimulSetup() {
        delete[] tier_cap;
        delete[] tier_load_lat;
        delete[] tier_4KB_R_lat;
        delete[] tier_4KB_W_lat;
    }
};

class SimulStruct {
public:
    int *mtable;              // va -> physical slot (-1 = unallocated)
    int *reference_bit;       // per-va hint-fault bit, cleared every phase
    int **mig_info;           // migration count per (src, dst) tier
    int *access_info;         // access count per tier
    int *alloc_info;          // allocation count per tier
    int *alloc_order;         // tier order tried on allocation
    int *access_cnt;          // MTM: sampled access counter
    int *fault_access_cnt;    // AutoTiering: 8-bit hint-fault history

    ~SimulStruct() {
        delete[] mtable;
        delete[] reference_bit;
        delete[] access_info;
        delete[] alloc_info;
        for (int i = 0; i < 4; ++i) {
            delete[] mig_info[i];
        }
        delete[] mig_info;
        delete[] alloc_order;
        delete[] access_cnt;
        delete[] fault_access_cnt;
    }
};

// One memory tier: LRU-ordered resident pages plus a free-slot pool.
class Tier {
public:
    int capacity;
    int usage;

    list<int> lru_list;
    unordered_map<int, list<int>::iterator> lru_map;
    queue<int> free_list;

    Tier(int cap = 0) {
        capacity = cap;
        usage = 0;
    }

    bool contains(int va) const {
        return lru_map.find(va) != lru_map.end();
    }

    bool remove(int va) {
        auto it = lru_map.find(va);
        if (it == lru_map.end()) return false;
        lru_list.erase(it->second);
        lru_map.erase(it);
        usage--;
        return true;
    }

    // touch: move va to the MRU position (inserting it if absent)
    void access(int va) {
        if (contains(va)) {
            lru_list.erase(lru_map[va]);
        } else {
            usage++;
        }

        lru_list.push_front(va);
        lru_map[va] = lru_list.begin();
    }

    int oldest() {
        if (!lru_list.empty()) {
            return lru_list.back();
        }
        return -1;
    }

    int evict_oldest() {
        if (!lru_list.empty()) {
            int old_va = lru_list.back();
            lru_list.pop_back();
            lru_map.erase(old_va);
            return old_va;
        }
        return -1;
    }

    void reclaim_free_page(int pa) {
        free_list.push(pa);
    }

    int allocate_free_page() {
        if (!free_list.empty()) {
            int va = free_list.front();
            free_list.pop();
            return va;
        } else {
            return -1;
        }
    }
};

// AutoTiering: hotness buckets for pages resident in the fast tier.
// min_bucket() gives the hotness of T0's coldest page, which a promotion
// candidate has to beat.
class AccessBuckets {
    static constexpr int MAX_BUCKET = 8;
    using Bucket     = list<int>;
    using BucketsArr = array<Bucket, MAX_BUCKET + 1>;
    using Pos        = pair<int, Bucket::iterator>;  // (bucket, node)

    BucketsArr buckets_;
    unordered_map<int, Pos> index_;

    int current_min_ = MAX_BUCKET + 1;   // MAX+1 = empty

    void recompute_min() {
        for (int i = 0; i <= MAX_BUCKET; ++i) {
            if (!buckets_[i].empty()) { current_min_ = i; return; }
        }
        current_min_ = MAX_BUCKET + 1;
    }

    void maybe_update_min_on_insert(int bucket) {
        if (bucket < current_min_) current_min_ = bucket;
    }
    void maybe_update_min_on_erase(int bucket) {
        if (bucket == current_min_ && buckets_[bucket].empty())
            recompute_min();
    }

public:
    void insert(int va, int bucket = 0) {
        auto it = buckets_[bucket].insert(buckets_[bucket].begin(), va);
        index_[va] = {bucket, it};
        maybe_update_min_on_insert(bucket);
    }

    void erase(int va) {
        auto itPos = index_.find(va);
        if (itPos == index_.end()) return;
        int bucket = itPos->second.first;
        buckets_[bucket].erase(itPos->second.second);
        index_.erase(itPos);
        maybe_update_min_on_erase(bucket);
    }

    void update(int va, int newBucket) {
        auto &p = index_[va];
        int oldBucket = p.first;
        if (oldBucket == newBucket) return;

        buckets_[oldBucket].erase(p.second);
        auto newIt = buckets_[newBucket].insert(buckets_[newBucket].begin(), va);
        p = {newBucket, newIt};

        maybe_update_min_on_erase(oldBucket);
        maybe_update_min_on_insert(newBucket);
    }

    void increment(int va) {
        update(va, min(MAX_BUCKET, index_[va].first + 1));
    }
    void decrement(int va) {
        auto cur = index_[va].first;
        if (cur > 0) update(va, cur - 1);
    }

    // lowest non-empty bucket, or -1 if empty
    int min_bucket() const {
        return (current_min_ <= MAX_BUCKET) ? current_min_ : -1;
    }
};

// MTM: exponential access-count histogram; bin i holds pages with counts in
// (2^(i-1), 2^i]. Ranking the bins from hot to cold orders every page.
class ExpHistogram {
public:
    static constexpr int MAX_POW   = 10;               // 2^10 = 1024
    static constexpr int BIN_COUNT = MAX_POW + 2;

    using Bucket     = list<int>;
    using BucketsArr = array<Bucket, BIN_COUNT>;
    using Pos        = pair<int, Bucket::iterator>;    // (bin, node)

    BucketsArr buckets_;
    unordered_map<int, Pos> index_;

    static int upper_inclusive(int bin) {
        if (bin == 0) return 0;
        if (bin == 1) return 1;
        if (bin < BIN_COUNT - 1) return (1 << bin) - 1;
        return INT_MAX;                                // last bin is unbounded
    }

    const list<int>& get_bin(int bin) const {
        return buckets_[bin];
    }

    void insert(int va, int bin) {
        if (bin < 0) bin = 0;
        if (bin >= BIN_COUNT) bin = BIN_COUNT - 1;
        auto old = index_.find(va);
        if (old != index_.end())            // upsert: drop the stale node
            buckets_[old->second.first].erase(old->second.second);
        auto it = buckets_[bin].insert(buckets_[bin].begin(), va);
        index_[va] = {bin, it};
    }

    int bin_count() const { return BIN_COUNT; }
    const list<int>& bin_elems(int bin) const { return buckets_[bin]; }

    // Move va up one bin when its access count exceeds the current bin's
    // upper bound (a single step per call).
    bool check_and_update(int va, int access_cnt) {
        auto it = index_.find(va);
        if (it == index_.end()) {           // first sampled access: start tracking
            insert(va, 1);
            return true;
        }

        int cur = it->second.first;
        if (access_cnt <= upper_inclusive(cur)) return false;

        if (cur == BIN_COUNT - 1) return false;

        int nxt = cur + 1;
        buckets_[cur].erase(it->second.second);
        auto newIt = buckets_[nxt].insert(buckets_[nxt].begin(), va);
        it->second = {nxt, newIt};

        return true;
    }

    // Cooling: move every page down one bin (bin 0 stays).
    void cooling() {
        for (int b = 1; b < BIN_COUNT; ++b) {
            auto it = buckets_[b].begin();
            while (it != buckets_[b].end()) {
                int va = *it;
                auto cur = it++;
                buckets_[b].erase(cur);

                auto newIt = buckets_[b - 1].insert(buckets_[b - 1].begin(), va);
                index_[va] = {b - 1, newIt};
            }
        }
    }
};

#endif // SIMUL_H
