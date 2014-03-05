/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include <algorithm>
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
	Delta deltas[NUM_DELTAS];
	int  delta_pointer;

	//Constructor
	Entry(Addr pc = 0, Addr addr = 0){
		this->pc = pc;
		this->last_address  = addr;
		this->last_prefetch = 0;

		for(int i = 0; i < NUM_DELTAS; ++i){
			this->deltas[i] = 0;
		}

		this->delta_pointer = 0;
	}

	//Construct function (object function)
	void insert_delta(Delta delta){
		this->deltas[this->delta_pointer] = delta;
		this->delta_pointer = (this->delta_pointer + 1) % NUM_DELTAS;
	}
};

//The global table holding all of the Entry rows for the DCPT implementation.
Entry table[TABLE_SIZE];

//wtf does this function do?
Entry &table_lookup(Addr pc){
	// Direct mapping. Could use other strategy like 2-way associative.
	return table[pc % TABLE_SIZE];
}

//wtf is it this one is supposed to do?
vector<Addr> delta_correlation(const Entry &entry){
	vector<Addr> candidates;

	Delta d1 = entry.deltas[(this->delta_pointer + NUM_DELTAS - 1) % NUM_DELTAS];
	Delta d2 = entry.deltas[(this->delta_pointer + NUM_DELTAS - 2) % NUM_DELTAS];

	Addr address = entry.last_address;

	for(/* each pair u, v in entry.deltas */)
	{
		if(entry.deltas[i] == d1 && entry.deltas[j] == d2)
		{
			for(/* each delta remaining in entry.deltas */)
			{
				address += delta;
				candidates.push_back(address);
			}
		}
	}

	return candidates;
}

//Filter what?
vector<Addr> prefetch_filter(const Entry &entry, const vector<Addr> &candidates){
	vector<Addr> prefetches;
	for (vector<candidates>::iterator i = candidates.begin(); i != candidates.end(); ++i){
		if i
	}
	return candidates;
}

//Function to issue prefetch command when we have found out that we don't have the data available in top-level cache
//(Or so I assume?)
void issue_prefetches(const vector<Addr> &prefetches){
	for_each(prefetches.begin(), prefetches.end(), issue_prefetch);
}

void prefetch_complete(Addr addr) {
	/*
	 * Called when a block requested by the prefetcher has been loaded.
	 */
}

void prefetch_init(void){
	/* Called before any calls to prefetch_access. */
	/* This is the place to initialize data structures. */

	DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
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

void prefetch_complete(Addr addr){
	/*
	 * Called when a block requested by the prefetcher has been loaded.
	 */
}

