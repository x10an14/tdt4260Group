/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include <algorithm>
#include <deque>
#include <set>
#include <vector>
#include "interface.hh"

// TODO: I just chose these constants somewhat arbitrarily. Should choose these more carefully.
#define NUM_DELTAS 8
#define TABLE_SIZE 128

 using namespace std;

typedef int Delta;

//Entry struct declaration (Entry == row in DCPT table)
struct Entry{
	Addr pc;
	Addr last_address;
	Addr last_prefetch;
	deque<Delta> deltas;

	//Constructor
	Entry(Addr pc = 0, Addr addr = 0){
		this->pc = pc;
		this->last_address  = addr;
		this->last_prefetch = 0;
	}

	//Construct function (object function)
	void insert_delta(Delta delta){
		deltas.push_back(delta);
		if(deltas.size() > NUM_DELTAS)
			deltas.pop_front();
	}
};

//The global table holding all of the Entry rows for the DCPT implementation.
Entry table[TABLE_SIZE];
set<Addr> in_flight;

//wtf does this function do?
Entry &table_lookup(Addr pc){
	// Direct mapping. Could use other strategy like 2-way associative.
	return table[pc % TABLE_SIZE];
}

//wtf is it this one is supposed to do?
vector<Addr> delta_correlation(const Entry &entry){
	vector<Addr> candidates;

    if(entry.deltas.size() < 2) {
        return candidates;
    }

    deque<Delta>::const_reverse_iterator u, v;
    u = entry.deltas.rbegin();
    v = u; ++v;

    Delta d1 = *u, d2 = *v;
	Addr address = entry.last_address;

	for(++u, ++v; v != entry.deltas.rend(); ++u, ++v) {
		if(*u == d1 && *v == d2) {
            deque<Delta>::const_reverse_iterator w = v;
			for(; w != entry.deltas.rend(); ++w) {
				address += *w;
				candidates.push_back(address);
			}
		}
	}

	return candidates;
}

vector<Addr> prefetch_filter(const Entry &entry, const vector<Addr> &candidates){
	vector<Addr> prefetches;
	for (vector<Addr>::const_iterator i = candidates.begin(); i != candidates.end(); ++i){
		if(in_flight.find(*i) == in_flight.end() && !in_mshr_queue(*i) && !in_cache(*i)){
            if(in_flight.size() < MAX_QUEUE_SIZE) {
                prefetches.push_back(*i);
                in_flight.insert(*i);
            }
		}
		if(*i == entry.last_prefetch){
			prefetches.clear();
		}
	}
	return prefetches;
}

//Function to issue prefetch command when we have found out that we don't have the data available in top-level cache
//(Or so I assume?)
void issue_prefetches(const vector<Addr> &prefetches){
	for_each(prefetches.begin(), prefetches.end(), issue_prefetch);
	//in_flight.insert(prefetches.begin(), prefetches.end());
}

void prefetch_complete(Addr addr) {
	/*
	 * Called when a block requested by the prefetcher has been loaded.
	 */
	in_flight.erase(addr);
}

void prefetch_init(void){
	/* Called before any calls to prefetch_access. */
	/* This is the place to initialize data structures. */

	DPRINTF(HWPrefetch, "Initialized DCPT prefetcher\n");
}

void prefetch_access(AccessStat stat){
	Addr pc   = stat.pc;
	Addr addr = stat.mem_addr;

	Entry &entry = table_lookup(pc);

	if(entry.pc != pc){
		entry = Entry(pc, addr);
	}
	else if(stat.mem_addr != entry.last_address){
		entry.insert_delta(addr - entry.last_address);
		entry.last_address = addr;

		vector<Addr> candidates = delta_correlation(entry);
		vector<Addr> prefetches = prefetch_filter(entry, candidates);
		issue_prefetches(prefetches);
	}
}

