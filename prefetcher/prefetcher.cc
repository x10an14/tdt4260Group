/*
 * A sample prefetcher which does sequential one-block lookahead.
 * This means that the prefetcher fetches the next block _after_ the one that
 * was just accessed. It also ignores requests to blocks already in the cache.
 */

#include "interface.hh"


void prefetch_init(void)
{
    /* Called before any calls to prefetch_access. */
    /* This is the place to initialize data structures. */

    DPRINTF(HWPrefetch, "Initialized sequential-on-access prefetcher\n");
}

void prefetch_access(AccessStat stat)
{
    /* pf_addr is now an address within the _next_ cache block */
    Addr pf_addr = stat.mem_addr + BLOCK_SIZE;

    /*
     * Issue a prefetch request if a demand miss occured,
     * and the block is not already in cache.
     */
    if (stat.miss && !in_cache(pf_addr)) {
        issue_prefetch(pf_addr);
    }
	//If prefetched data is accessed, prefetch the next 5 blocks
	else if (!stat.miss && in_cache(pf_addr) && get_prefetch_bit(pf_addr)){
		for (int i = 1; i < 6; i++){
			if (!in_cache(pf_addr + BLOCK_SIZE * i))
				issue_prefetch(pf_addr + BLOCK_SIZE * i);
		}
	}
}

void prefetch_complete(Addr addr) {
	set_prefetch_bit(addr);
}
