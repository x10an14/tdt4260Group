/*
 * Author: Hallvard Norheim Bø
 * NTNU, June 2010
 */

/**
 * @file
 * Describes a sequential prefetcher based on template policies.
 */

#include "base/trace.hh"
#include "arch/isa_traits.hh"
#include "mem/cache/prefetch/sequential.hh"

SequentialPrefetcher::SequentialPrefetcher(const BaseCacheParams *p)
    : BasePrefetcher(p),
      latency(p->prefetch_latency), degree(p->prefetch_degree),
      useContextId(p->prefetch_use_cpu_id)
{
    DPRINTF(HWPrefetch, "Sequential one-block-lookahead prefetcher\n");
    DPRINTF(HWPrefetch, "prefetch on access: %d\n");
    if (p->prefetch_on_access)
        DPRINTF(HWPrefetch, "Strategy: prefetch-on-access\n");
    else
        DPRINTF(HWPrefetch, "Strategy: prefetch-on-miss\n");
}

void
SequentialPrefetcher::
calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                  std::list<Tick> &delays)
{
    Addr addr = pkt->getAddr() & ~(Addr)(blkSize-1);
    addr += blkSize;

    //assert(pkt->isRead());

    if (useContextId && !pkt->req->hasContextId()) {
        /* writebacks seems to be the source of requests without cid */
        DPRINTF(HWPrefetch ,"ignoring request with no context ID\n");
        return;
    } else if (pkt->req->isPrefetch()) {
        DPRINTF(HWPrefetch, "ignoring; mem request is already a pf request\n");
        return;
    } else if (inCache(addr)) {
        DPRINTF(HWPrefetch, "ignoring; mem request is already in cache\n");
        return;
    } else {
        DPRINTF(HWPrefetch, "adding a new pf request to %#x\n", addr);
        addresses.push_back(addr);
        delays.push_back(latency);
    }
}
