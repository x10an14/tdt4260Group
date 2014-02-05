#include "mem/cache/prefetch/cppinterface_outer.hh"
#include <stdio.h>

using namespace PrefetcherInterface;

Prefetcher::Prefetcher(Cache &_cache)
        : cache(_cache), blkSize(_cache.getBlockSize())
{
}

void
Prefetcher::notify(Request &request)
{
    /*
    if (!request.hit || cache.getPrefetchBit(request.address)) {
        cache.queuePrefetch(request.address + cache.getBlockSize());
    } else {
        cache.setPrefetchBit(request.address, false);
    }
    printf("Cache size %d\n", cache.getBlockSize());
    */
    if (!request.hit)
        for (int i = 1; i <= 8; i++) {
            cache.queuePrefetch(request.address + blkSize * i);
        }
}
