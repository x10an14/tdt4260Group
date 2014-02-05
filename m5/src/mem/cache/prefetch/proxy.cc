/*
 * Author: Hallvard Norheim Bø
 * NTNU, June 2010
 */

/**
 * @file
 * Subclasses BasePrefetcher, and does the dirty work needed for the
 * simplified prefetcher interface to present a clean C interface.
 */

#include <assert.h>
#include <stdio.h>
#include "mem/cache/prefetch/proxy.hh"
#include "base/misc.hh"     /*fatal, panic, warn*/

ProxyPrefetcher * _pp;

ProxyPrefetcher::ProxyPrefetcher(const BaseCacheParams *p)
    : BasePrefetcher(p),
      useContextId(p->prefetch_use_cpu_id),
      latency(p->prefetch_latency)
{
    printf("Creating prefetcher proxy\n");
    _pp = this;
    _addresses = 0;
    _delays = 0;
    isInitialized = false;
}

void
ProxyPrefetcher::
calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                  std::list<Tick> &delays)
{
    if (!pkt->req->hasPC())
        return;
    if (!pkt->req->hasContextId())
        return;

    if (!isInitialized) {
        isInitialized = true;
        prefetch_init();
    }

    _addresses = &addresses;
    _delays = &delays;
    Addr addr = pkt->getAddr();

    AccessStat stat;
    // triggers an assertion if pkt->req->hasPC() is false
    stat.pc = pkt->req->getPC();
    stat.mem_addr = addr;
    stat.time = pkt->time;
    stat.miss = !inCache(addr);

    prefetch_access(stat);
}

void
ProxyPrefetcher::
prefetchComplete(PacketPtr &pkt)
{
    /*
     * prefetchComplete is only ever called as a response to
     * a prefetch request originating from a user-prefetcher,
     * thus isInitialized should always be true.
     */
    assert(isInitialized);

    prefetch_complete(pkt->getAddr());
}


/* Implementations of the C-style functions from interface.hh */

void issue_prefetch(Addr addr)
{
    Addr blk_addr = addr & ~(Addr)(BLOCK_SIZE - 1);

    assert(_pp != 0);
    assert(_pp->_addresses != 0);
    assert(_pp->_delays != 0);

    if (addr <= MAX_PHYS_MEM_ADDR) {
        /* push a properly aligned address */
        _pp->_addresses->push_back(blk_addr);
        _pp->_delays->push_back(_pp->latency);
    } else {
        fatal("physical address too large: %#x\n", addr);
    }
}

int get_prefetch_bit(Addr addr)
{
    return _pp->getPrefetchBit(addr & ~(Addr)(BLOCK_SIZE - 1));
}

void set_prefetch_bit(Addr addr)
{
    _pp->setPrefetchBit(addr & ~(Addr)(BLOCK_SIZE - 1));
}

void clear_prefetch_bit(Addr addr)
{
    _pp->clearPrefetchBit(addr & ~(Addr)(BLOCK_SIZE - 1));
}

int in_cache(Addr addr)
{
    return _pp->inCache(addr & ~(Addr)(BLOCK_SIZE - 1));
}

int in_mshr_queue(Addr addr)
{
    return _pp->inMissQueue(addr & ~(Addr)(BLOCK_SIZE - 1));
}

int current_queue_size(void)
{
    return _pp->currentQueueSize();
}
