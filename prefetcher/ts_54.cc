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
#define DISTANCE 5
#endif

void prefetch_init(void)
{
    /* Called before any calls to prefetch_access. */
    /* This is the place to initialize data structures. */

    DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

//prefetching with degree and distance.

void prefetch_several(Addr address)
{
    for (unsigned int i = 0; i < degree; i++){
        Addr pf_addr = address + BLOCK_SIZE * (degree + distance);
        if (!in_cache(pf_addr))
            issue_prefetch(pf_addr);
    }
}

void prefetch_access(AccessStat stat)
{
    /* pf_addr is now an address within the _next_ cache block */
    Addr pf_addr = stat.mem_addr + BLOCK_SIZE;

    /*
     * Issue a prefetch request if a demand miss occured,
     * and the block is not already in cache.
     */

    //degree = 5, distance = 4. Dette nevnes som en konfigurasjon i Marius Grannaes sin paper.
    //Vet ikke helt om jeg har tolket distance-parameteren riktig da.
    if (stat.miss) {
        prefetch_several(stat.mem_addr);
    }
    //If prefetched data is accessed, prefetch the next block
    else if (!stat.miss && get_prefetch_bit(stat.mem_addr)){
	   prefetch_several(stat.mem_addr);
    }
}

void prefetch_complete(Addr addr) {
	set_prefetch_bit(addr);
}

