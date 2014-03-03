/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include "interface.hh"

// TODO: I just chose these constants somewhat arbitrarily. Should choose these more carefully.
#define NUM_DELTAS 8
#define TABLE_SIZE 128

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
std::vector<Addr> delta_correlation(const Entry &entry){
	// TODO: implement
	return std::vector<Addr>();
}

//Filter what?
std::vector<Addr> prefetch_filter(const Entry &entry, const std::vector<Addr> &candidates){
	// TODO: implement
	return candidates;
}

//Function to issue prefetch command when we have found out that we don't have the data available in top-level cache
//(Or so I assume?)
void issue_prefetches(const std::vector<Addr> &prefetches){
	// TODO: implement
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

		std::vector<Addr> candidates = delta_correlation(entry);
		std::vector<Addr> prefetches = prefetch_filter(entry, candidates);
		issue_prefetches(prefetches);
	}
}

void prefetch_complete(Addr addr){
	/*
	 * Called when a block requested by the prefetcher has been loaded.
	 */
}

