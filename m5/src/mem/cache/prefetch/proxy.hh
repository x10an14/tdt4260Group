/*
 * Author: Hallvard Norheim Bø
 * NTNU, June 2010
 */

/**
 * @file
 * Subclasses BasePrefetcher, and contains the necessary hooks for the
 * simplified interface.
 */

#ifndef __MEM_CACHE_PREFETCH_PROXY_PREFETCHER_HH__
#define __MEM_CACHE_PREFETCH_PROXY_PREFETCHER_HH__

#include "mem/cache/prefetch/base.hh"
#include "mem/cache/prefetch/interface.hh"


class ProxyPrefetcher : public BasePrefetcher
{
  private:

    bool useContextId;
    bool isInitialized;

  public:

    Tick latency;
    std::list<Addr> *_addresses;
    std::list<Tick> *_delays;

    ProxyPrefetcher(const BaseCacheParams *p);

    ~ProxyPrefetcher() {}

    void calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                           std::list<Tick> &delays);

    void prefetchComplete(PacketPtr &pkt);
};

extern ProxyPrefetcher * _pp;


#endif // __MEM_CACHE_PREFETCH_PROXY_PREFETCHER_HH__
