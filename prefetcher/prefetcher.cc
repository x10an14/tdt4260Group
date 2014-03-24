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

// We have 8KiB for the entry table. Each entry is 3 * 64 bits for the address
// fields, plus NUM_DELTAS * DELTA_BITS bits for the deltas themselves.
//
// Example configuration: 8 deltas, each 8 bits = 4 * 64 bits per entry = 32 byte
// per entry. 8192 / 32 = room for 256 entries in the table. However, in hardware
// we would not store the final 8 bits of the PC, as they are implicit from the
// position in the table. These extra 8 bits have been used to store one more
// delta per entry.
#define NUM_DELTAS 9
#define TABLE_SIZE 256
#define DELTA_BITS 8

#define DELTA_MIN (- (1 << (DELTA_BITS - 1)))
#define DELTA_MAX ((1 << (DELTA_BITS - 1)) - 1)

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
        // Ignore deltas too large or too negative to store.
        if(delta < DELTA_MIN || delta > DELTA_MAX) {
            return; 
        }

		deltas.push_back(delta);
		if(deltas.size() > NUM_DELTAS)
			deltas.pop_front();
	}
};

//The global table holding all of the Entry rows for the DCPT implementation.
Entry table[TABLE_SIZE];
set<Addr> in_flight;

// Round addresses to multiple of the block size so that we can recognize
// addresses within the same block in the in_flight array.
Addr to_block(Addr addr) {
    return (addr / BLOCK_SIZE) * BLOCK_SIZE;
}

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
		if(in_flight.find(to_block(*i)) == in_flight.end() && !in_mshr_queue(*i) && !in_cache(*i)){
            if(in_flight.size() < MAX_QUEUE_SIZE) {
                prefetches.push_back(*i);
                in_flight.insert(to_block(*i));
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
    for(vector<Addr>::const_iterator i = prefetches.begin(); i != prefetches.end(); ++i) {
        if(current_queue_size() >= MAX_QUEUE_SIZE)
            break;

        issue_prefetch(*i);
    }
}

void prefetch_complete(Addr addr) {
	/*
	 * Called when a block requested by the prefetcher has been loaded.
	 */
	in_flight.erase(to_block(addr));
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

