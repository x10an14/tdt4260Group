#ifndef __MEM_CACHE_PREFETCH_INTERFACE_PREFETCHER_HH__
#define __MEM_CACHE_PREFETCH_INTERFACE_PREFETCHER_HH__

#include "mem/cache/prefetch/base.hh"
#include "mem/cache/prefetch/cppinterface_inner.hh"

class InterfacePrefetcher : public BasePrefetcher
{
  private:

    Tick latency;
    int degree;
    bool useContextId;

    PrefetcherInterface::PrefetcherProxy *prefetcher;
    PrefetcherInterface::Cache cacheProxy;

    std::list<Addr> *addr_list;
    std::list<Tick> *delay_list;

  public:

    InterfacePrefetcher(const BaseCacheParams *p)
        : BasePrefetcher(p), latency(p->prefetch_latency),
          degree(p->prefetch_degree), useContextId(p->prefetch_use_cpu_id),
          prefetcher(NULL),
          addr_list(NULL), delay_list(NULL)
    {
        DPRINTF(HWPrefetch, "Initing IFPrefetcher\n");
        cacheProxy.impl = this;
    }

    ~InterfacePrefetcher() {
        if (prefetcher)
            delete prefetcher;
    }

    void calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                           std::list<Tick> &delays);

    void queuePrefetch(Addr address);
    bool inCache(Addr address);
    bool inQueue(Addr address);

    bool getPrefetchBit(Addr address);
    void setPrefetchBit(Addr address, bool value);

    uint32_t getBlockSize();
};

#endif // __MEM_CACHE_PREFETCH_INTERFACE_PREFETCHER_HH__
