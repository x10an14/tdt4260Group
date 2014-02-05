/*
 * Author: Hallvard Norheim Bø
 * NTNU, June 2010
 */

/**
 * @file
 * Describes a sequential prefetcher.
 */

#ifndef __MEM_CACHE_PREFETCH_SEQUENTIAL_PREFETCHER_HH__
#define __MEM_CACHE_PREFETCH_SEQUENTIAL_PREFETCHER_HH__

#include "mem/cache/prefetch/base.hh"

class SequentialPrefetcher : public BasePrefetcher
{
  protected:

    Tick latency;
    int degree;
    bool useContextId;

  public:

    SequentialPrefetcher(const BaseCacheParams *p);

    ~SequentialPrefetcher() {}

    void calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                           std::list<Tick> &delays);
};

#endif // __MEM_CACHE_PREFETCH_SEQUENTIAL_PREFETCHER_HH__
