#ifndef __INTERFACE_HH__
#define __INTERFACE_HH__

#include <stdint.h>
#include <stdio.h>

namespace PrefetcherInterface {

// Forward declarations
class Cache;
class Request;

/*
 * This class can be extended as you require.
 */

class Prefetcher
{
  private:
    Cache &cache;
    int32_t blkSize;

  public:
    Prefetcher(Cache &_cache);
    void notify(Request &request);
};


/*
 * You should not change anything below here.
 */

typedef uint64_t Addr;
typedef int64_t Tick;

class Request
{
  public:
    bool hit;
    Addr address;
    Addr pc;
};

class Cache
{
  private:
    void *impl;

  public:
    void queuePrefetch(Addr address);

    bool inCache(Addr address);
    bool inQueue(Addr address);

    bool getPrefetchBit(Addr address);
    bool setPrefetchBit(Addr address, bool value);

    uint32_t getBlockSize();
};


/*
 * For internal use, do not touch.
 */

class PrefetcherProxy
{
  private:
    Prefetcher *impl;

  public:
    PrefetcherProxy(Cache &cache);
    ~PrefetcherProxy();
    void notify(Request &request);
};


/*
 * Implementation is here to force emission of object code.
 * Could cause problems if you include the header file more than once.
 */
PrefetcherProxy::PrefetcherProxy(Cache &cache) { impl = new Prefetcher(cache); }
PrefetcherProxy::~PrefetcherProxy() { delete impl; }
void PrefetcherProxy::notify(Request &request) { impl->notify(request); };

} /* namespace PrefetcherInterface */

#endif /* __INTERFACE_HH__ */
