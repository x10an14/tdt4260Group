/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Erik Hallnor
 *          Dave Greene
 *          Steve Reinhardt
 *          Ron Dreslinski
 */

/**
 * @file
 * Describes a cache based on template policies.
 */

#ifndef __CACHE_HH__
#define __CACHE_HH__

#include "base/misc.hh" // fatal, panic, and warn

#include "mem/cache/base.hh"
#include "mem/cache/blk.hh"
#include "mem/cache/mshr.hh"

#include "sim/eventq.hh"

//Forward decleration
class BasePrefetcher;

/**
 * A template-policy based cache. The behavior of the cache can be altered by
 * supplying different template policies. TagStore handles all tag and data
 * storage @sa TagStore.
 */
template <class TagStore>
class Cache : public BaseCache
{
  public:
    /** Define the type of cache block to use. */
    typedef typename TagStore::BlkType BlkType;
    /** A typedef for a list of BlkType pointers. */
    typedef typename TagStore::BlkList BlkList;

  protected:

    class CpuSidePort : public CachePort
    {
      public:
        CpuSidePort(const std::string &_name,
                    Cache<TagStore> *_cache,
                    const std::string &_label);

        // BaseCache::CachePort just has a BaseCache *; this function
        // lets us get back the type info we lost when we stored the
        // cache pointer there.
        Cache<TagStore> *myCache() {
            return static_cast<Cache<TagStore> *>(cache);
        }

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
                                            bool &snoop);

        virtual bool recvTiming(PacketPtr pkt);

        virtual Tick recvAtomic(PacketPtr pkt);

        virtual void recvFunctional(PacketPtr pkt);
    };

    class MemSidePort : public CachePort
    {
      public:
        MemSidePort(const std::string &_name,
                    Cache<TagStore> *_cache,
                    const std::string &_label);

        // BaseCache::CachePort just has a BaseCache *; this function
        // lets us get back the type info we lost when we stored the
        // cache pointer there.
        Cache<TagStore> *myCache() {
            return static_cast<Cache<TagStore> *>(cache);
        }

        void sendPacket();

        void processSendEvent();

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
                                            bool &snoop);

        virtual bool recvTiming(PacketPtr pkt);

        virtual void recvRetry();

        virtual Tick recvAtomic(PacketPtr pkt);

        virtual void recvFunctional(PacketPtr pkt);

        typedef EventWrapper<MemSidePort, &MemSidePort::processSendEvent>
                SendEvent;
    };

    /** Tag and data Storage */
    TagStore *tags;

    /** Prefetcher */
    BasePrefetcher *prefetcher;

    /** Temporary cache block for occasional transitory use */
    BlkType *tempBlock;

    /**
     * This cache should allocate a block on a line-sized write miss.
     */
    const bool doFastWrites;

    /**
     * Notify the prefetcher on every access, not just misses.
     */
    const bool prefetchOnAccess;

    /**
     * Does all the processing necessary to perform the provided request.
     * @param pkt The memory request to perform.
     * @param lat The latency of the access.
     * @param writebacks List for any writebacks that need to be performed.
     * @param update True if the replacement data should be updated.
     * @return Boolean indicating whether the request was satisfied.
     */
    bool access(PacketPtr pkt, BlkType *&blk,
                int &lat, PacketList &writebacks);

    /**
     *Handle doing the Compare and Swap function for SPARC.
     */
    void cmpAndSwap(BlkType *blk, PacketPtr pkt);

    /**
     * Find a block frame for new block at address addr, assuming that
     * the block is not currently in the cache.  Append writebacks if
     * any to provided packet list.  Return free block frame.  May
     * return NULL if there are no replaceable blocks at the moment.
     */
    BlkType *allocateBlock(Addr addr, PacketList &writebacks);

    /**
     * Populates a cache block and handles all outstanding requests for the
     * satisfied fill request. This version takes two memory requests. One
     * contains the fill data, the other is an optional target to satisfy.
     * @param pkt The memory request with the fill data.
     * @param blk The cache block if it already exists.
     * @param writebacks List for any writebacks that need to be performed.
     * @return Pointer to the new cache block.
     */
    BlkType *handleFill(PacketPtr pkt, BlkType *blk,
                        PacketList &writebacks);

    void satisfyCpuSideRequest(PacketPtr pkt, BlkType *blk);
    bool satisfyMSHR(MSHR *mshr, PacketPtr pkt, BlkType *blk);

    void doTimingSupplyResponse(PacketPtr req_pkt, uint8_t *blk_data,
                                bool already_copied, bool pending_inval);

    /**
     * Sets the blk to the new state.
     * @param blk The cache block being snooped.
     * @param new_state The new coherence state for the block.
     */
    void handleSnoop(PacketPtr ptk, BlkType *blk,
                     bool is_timing, bool is_deferred, bool pending_inval);

    /**
     * Create a writeback request for the given block.
     * @param blk The block to writeback.
     * @return The writeback request for the block.
     */
    PacketPtr writebackBlk(BlkType *blk);

  public:
    /** Instantiates a basic cache object. */
    Cache(const Params *p, TagStore *tags, BasePrefetcher *prefetcher);

    virtual Port *getPort(const std::string &if_name, int idx = -1);
    virtual void deletePortRefs(Port *p);

    void regStats();

    /**
     * Performs the access specified by the request.
     * @param pkt The request to perform.
     * @return The result of the access.
     */
    bool timingAccess(PacketPtr pkt);

    /**
     * Performs the access specified by the request.
     * @param pkt The request to perform.
     * @return The result of the access.
     */
    Tick atomicAccess(PacketPtr pkt);

    /**
     * Performs the access specified by the request.
     * @param pkt The request to perform.
     * @return The result of the access.
     */
    void functionalAccess(PacketPtr pkt, CachePort *incomingPort,
                          CachePort *otherSidePort);

    /**
     * Handles a response (cache line fill/write ack) from the bus.
     * @param pkt The request being responded to.
     */
    void handleResponse(PacketPtr pkt);

    /**
     * Snoops bus transactions to maintain coherence.
     * @param pkt The current bus transaction.
     */
    void snoopTiming(PacketPtr pkt);

    /**
     * Snoop for the provided request in the cache and return the estimated
     * time of completion.
     * @param pkt The memory request to snoop
     * @return The estimated completion time.
     */
    Tick snoopAtomic(PacketPtr pkt);

    /**
     * Squash all requests associated with specified thread.
     * intended for use by I-cache.
     * @param threadNum The thread to squash.
     */
    void squash(int threadNum);

    /**
     * Generate an appropriate downstream bus request packet for the
     * given parameters.
     * @param cpu_pkt  The upstream request that needs to be satisfied.
     * @param blk The block currently in the cache corresponding to
     * cpu_pkt (NULL if none).
     * @param needsExclusive  Indicates that an exclusive copy is required
     * even if the request in cpu_pkt doesn't indicate that.
     * @return A new Packet containing the request, or NULL if the
     * current request in cpu_pkt should just be forwarded on.
     */
    PacketPtr getBusPacket(PacketPtr cpu_pkt, BlkType *blk,
                           bool needsExclusive);

    /**
     * Return the next MSHR to service, either a pending miss from the
     * mshrQueue, a buffered write from the write buffer, or something
     * from the prefetcher.  This function is responsible for
     * prioritizing among those sources on the fly.
     */
    MSHR *getNextMSHR();

    /**
     * Selects an outstanding request to service.  Called when the
     * cache gets granted the downstream bus in timing mode.
     * @return The request to service, NULL if none found.
     */
    PacketPtr getTimingPacket();

    /**
     * Marks a request as in service (sent on the bus). This can have side
     * effect since storage for no response commands is deallocated once they
     * are successfully sent.
     * @param pkt The request that was sent on the bus.
     */
    void markInService(MSHR *mshr);

    /**
     * Perform the given writeback request.
     * @param pkt The writeback request.
     */
    void doWriteback(PacketPtr pkt);

    /**
     * Return whether there are any outstanding misses.
     */
    bool outstandingMisses() const
    {
        return mshrQueue.allocated != 0;
    }

    CacheBlk *findBlock(Addr addr) {
        return tags->findBlock(addr);
    }

    bool inCache(Addr addr) {
        return (tags->findBlock(addr) != 0);
    }

    bool inMissQueue(Addr addr) {
        return (mshrQueue.findMatch(addr) != 0);
    }

    bool getPrefetchBit(Addr addr) {
        CacheBlk *blk = findBlock(addr);
        return blk && (blk->status & BlkHWPrefetchFlag) != 0;
    }

    void setPrefetchBit(Addr addr) {
        CacheBlk *blk = findBlock(addr);
        if (blk)
            blk->status |= BlkHWPrefetchFlag;
    }

    void clearPrefetchBit(Addr addr) {
        CacheBlk *blk = findBlock(addr);
        if (blk)
            blk->status &= ~BlkHWPrefetchFlag;
    }

    /**
     * Find next request ready time from among possible sources.
     */
    Tick nextMSHRReadyTime();
};

#endif // __CACHE_HH__
