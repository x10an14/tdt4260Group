/*
 * Describes
 */

#include "base/trace.hh"
#include "mem/cache/base.hh"
#include "mem/cache/prefetch/cppinterface.hh"
#include "mem/cache/prefetch/cppinterface_inner.hh"

void
InterfacePrefetcher::calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                                 std::list<Tick> &delays)
{
    if (useContextId && !pkt->req->hasContextId()) {
        DPRINTF(HWPrefetch, "ignoring request with no context ID\n");
        return;
    } else if (!pkt->req->hasPC()) {
        DPRINTF(HWPrefetch, "ignoring request with no PC\n");
        return;
    }

    // Delay instantiation so the cache will be fully set up when the user constructor is called.
    if (!prefetcher)
        prefetcher = new PrefetcherInterface::PrefetcherProxy(cacheProxy);

    DPRINTF(HWPrefetch, "Entering notify\n");

    addr_list = &addresses;
    delay_list = &delays;

    PrefetcherInterface::Request request;
    DPRINTF(HWPrefetch, "Initing request\n");
    request.address = pkt->getAddr();
    request.hit = inCache(request.address);
    request.pc = pkt->req->getPC();
    // request.cycle = 0;

    prefetcher->notify(request);

    addr_list = NULL;
    delay_list = NULL;
    DPRINTF(HWPrefetch, "Exiting notify\n");
}

void
InterfacePrefetcher::queuePrefetch(Addr addr)
{
    assert(addr_list != NULL);
    addr_list->push_back(addr);
    delay_list->push_back(latency);
}

bool
InterfacePrefetcher::inCache(Addr addr)
{
    return cache->inCache(addr);
}

bool
InterfacePrefetcher::inQueue(Addr addr)
{
    return inPrefetch(addr) != pf.end();
}

bool
InterfacePrefetcher::getPrefetchBit(Addr addr)
{
    return cache->getPrefetchBit(addr);
}

void
InterfacePrefetcher::setPrefetchBit(Addr addr, bool value)
{
    if (value)
        cache->setPrefetchBit(addr);
    else
        cache->clearPrefetchBit(addr);
}

uint32_t
InterfacePrefetcher::getBlockSize()
{
    return blkSize;
}



// Cache methods

void
PrefetcherInterface::Cache::queuePrefetch(Addr address) {
    ((InterfacePrefetcher*)impl)->queuePrefetch(address);
}

bool
PrefetcherInterface::Cache::inCache(Addr address) {
    return ((InterfacePrefetcher*)impl)->inCache(address);
}

bool
PrefetcherInterface::Cache::inQueue(Addr address) {
    return ((InterfacePrefetcher*)impl)->inQueue(address);
}

bool
PrefetcherInterface::Cache::getPrefetchBit(Addr address) {
    return ((InterfacePrefetcher*)impl)->getPrefetchBit(address);
}

void
PrefetcherInterface::Cache::setPrefetchBit(Addr address, bool value) {
    ((InterfacePrefetcher*)impl)->setPrefetchBit(address, value);
}

uint32_t
PrefetcherInterface::Cache::getBlockSize() {
    return ((InterfacePrefetcher*)impl)->getBlockSize();
}
