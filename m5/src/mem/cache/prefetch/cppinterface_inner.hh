#ifndef __MEM_CACHE_PREFETCH_INTERFACE_INNER_HH__
#define __MEM_CACHE_PREFETCH_INTERFACE_INNER_HH__

#include "mem/cache/prefetch/cppinterface.hh"

namespace PrefetcherInterface {

class Request
{
  public:
    bool hit;
    Addr address;
    Addr pc;
};

class Cache
{
  public:
    void *impl;

  public:
    void queuePrefetch(Addr address);

    bool inCache(Addr address);
    bool inQueue(Addr address);

    bool getPrefetchBit(Addr address);
    void setPrefetchBit(Addr address, bool value);

    uint32_t getBlockSize();
};

class Prefetcher;

class PrefetcherProxy
{
  private:
    Prefetcher *impl;

  public:
    PrefetcherProxy(Cache &cache);
    ~PrefetcherProxy();
    void notify(Request &request);
};

} /* namespace PrefetcherInterface */

#endif /* __MEM_CACHE_PREFETCH_INTERFACE_INNER_HH__ */
