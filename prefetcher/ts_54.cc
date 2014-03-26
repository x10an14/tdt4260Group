/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include "interface.hh"
#ifndef DEGREE
#define DEGREE 5
#endif

#ifndef DISTANCE
#define DISTANCE 3
#endif

void prefetch_init(void)
{
    DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

//prefetching with DEGREE and DISTANCE.
void prefetch_several(Addr address)
{
    for (unsigned int i = 0; i < DEGREE; i++){
        Addr pf_addr = address + BLOCK_SIZE * (i + DISTANCE);
        if (!in_cache(pf_addr))
            issue_prefetch(pf_addr);
    }
}

void prefetch_access(AccessStat stat)
{
    Addr pf_addr = stat.mem_addr + BLOCK_SIZE;
    if (stat.miss) {
        prefetch_several(stat.mem_addr);
    }
    else if (get_prefetch_bit(stat.mem_addr)){
	   prefetch_several(stat.mem_addr);
    }
}

void prefetch_complete(Addr addr) {
	set_prefetch_bit(addr);
}

