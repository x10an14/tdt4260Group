/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * Copyright (c) 2010 Advanced Micro Devices, Inc.
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
 *          Nathan Binkert
 *          Steve Reinhardt
 *          Ron Dreslinski
 */

/**
 * @file
 * Cache definitions.
 */

#include "base/fast_alloc.hh"
#include "base/misc.hh"
#include "base/range.hh"
#include "base/types.hh"
#include "mem/cache/blk.hh"
#include "mem/cache/cache.hh"
#include "mem/cache/mshr.hh"
#include "mem/cache/prefetch/base.hh"
#include "sim/sim_exit.hh"

template<class TagStore>
Cache<TagStore>::Cache(const Params *p, TagStore *tags, BasePrefetcher *pf)
    : BaseCache(p),
      tags(tags),
      prefetcher(pf),
      doFastWrites(true),
      prefetchOnAccess(p->prefetch_on_access)
{
    tempBlock = new BlkType();
    tempBlock->data = new uint8_t[blkSize];

    cpuSidePort = new CpuSidePort(p->name + "-cpu_side_port", this,
                                  "CpuSidePort");
    memSidePort = new MemSidePort(p->name + "-mem_side_port", this,
                                  "MemSidePort");
    cpuSidePort->setOtherPort(memSidePort);
    memSidePort->setOtherPort(cpuSidePort);

    tags->setCache(this);
    if (prefetcher)
        prefetcher->setCache(this);
}

template<class TagStore>
void
Cache<TagStore>::regStats()
{
    BaseCache::regStats();
    tags->regStats(name());
    if (prefetcher)
        prefetcher->regStats(name());
}

template<class TagStore>
Port *
Cache<TagStore>::getPort(const std::string &if_name, int idx)
{
    if (if_name == "" || if_name == "cpu_side") {
        return cpuSidePort;
    } else if (if_name == "mem_side") {
        return memSidePort;
    } else if (if_name == "functional") {
        CpuSidePort *funcPort =
            new CpuSidePort(name() + "-cpu_side_funcport", this,
                            "CpuSideFuncPort");
        funcPort->setOtherPort(memSidePort);
        return funcPort;
    } else {
        panic("Port name %s unrecognized\n", if_name);
    }
}

template<class TagStore>
void
Cache<TagStore>::deletePortRefs(Port *p)
{
    if (cpuSidePort == p || memSidePort == p)
        panic("Can only delete functional ports\n");

    delete p;
}


template<class TagStore>
void
Cache<TagStore>::cmpAndSwap(BlkType *blk, PacketPtr pkt)
{
    uint64_t overwrite_val;
    bool overwrite_mem;
    uint64_t condition_val64;
    uint32_t condition_val32;

    int offset = tags->extractBlkOffset(pkt->getAddr());
    uint8_t *blk_data = blk->data + offset;

    assert(sizeof(uint64_t) >= pkt->getSize());

    overwrite_mem = true;
    // keep a copy of our possible write value, and copy what is at the
    // memory address into the packet
    pkt->writeData((uint8_t *)&overwrite_val);
    pkt->setData(blk_data);

    if (pkt->req->isCondSwap()) {
        if (pkt->getSize() == sizeof(uint64_t)) {
            condition_val64 = pkt->req->getExtraData();
            overwrite_mem = !std::memcmp(&condition_val64, blk_data,
                                         sizeof(uint64_t));
        } else if (pkt->getSize() == sizeof(uint32_t)) {
            condition_val32 = (uint32_t)pkt->req->getExtraData();
            overwrite_mem = !std::memcmp(&condition_val32, blk_data,
                                         sizeof(uint32_t));
        } else
            panic("Invalid size for conditional read/write\n");
    }

    if (overwrite_mem) {
        std::memcpy(blk_data, &overwrite_val, pkt->getSize());
        blk->status |= BlkDirty;
    }
}


template<class TagStore>
void
Cache<TagStore>::satisfyCpuSideRequest(PacketPtr pkt, BlkType *blk)
{
    assert(blk);
    // Occasionally this is not true... if we are a lower-level cache
    // satisfying a string of Read and ReadEx requests from
    // upper-level caches, a Read will mark the block as shared but we
    // can satisfy a following ReadEx anyway since we can rely on the
    // Read requester(s) to have buffered the ReadEx snoop and to
    // invalidate their blocks after receiving them.
    // assert(pkt->needsExclusive() ? blk->isWritable() : blk->isValid());
    assert(pkt->getOffset(blkSize) + pkt->getSize() <= blkSize);

    // Check RMW operations first since both isRead() and
    // isWrite() will be true for them
    if (pkt->cmd == MemCmd::SwapReq) {
        cmpAndSwap(blk, pkt);
    } else if (pkt->isWrite()) {
        if (blk->checkWrite(pkt)) {
            pkt->writeDataToBlock(blk->data, blkSize);
            blk->status |= BlkDirty;
        }
    } else if (pkt->isRead()) {
        if (pkt->isLLSC()) {
            blk->trackLoadLocked(pkt);
        }
        pkt->setDataFromBlock(blk->data, blkSize);
        if (pkt->getSize() == blkSize) {
            // special handling for coherent block requests from
            // upper-level caches
            if (pkt->needsExclusive()) {
                // on ReadExReq we give up our copy
                tags->invalidateBlk(blk);
            } else {
                // on ReadReq we create shareable copies here and in
                // the requester
                pkt->assertShared();
                blk->status &= ~BlkWritable;
            }
        }
    } else {
        // Not a read or write... must be an upgrade.  it's OK
        // to just ack those as long as we have an exclusive
        // copy at this level.
        assert(pkt->isUpgrade());
        tags->invalidateBlk(blk);
    }
}


/////////////////////////////////////////////////////
//
// MSHR helper functions
//
/////////////////////////////////////////////////////


template<class TagStore>
void
Cache<TagStore>::markInService(MSHR *mshr)
{
    markInServiceInternal(mshr);
#if 0
        if (mshr->originalCmd == MemCmd::HardPFReq) {
            DPRINTF(HWPrefetch, "%s:Marking a HW_PF in service\n",
                    name());
            //Also clear pending if need be
            if (!prefetcher->havePending())
            {
                deassertMemSideBusRequest(Request_PF);
            }
        }
#endif
}


template<class TagStore>
void
Cache<TagStore>::squash(int threadNum)
{
    bool unblock = false;
    BlockedCause cause = NUM_BLOCKED_CAUSES;

    if (noTargetMSHR && noTargetMSHR->threadNum == threadNum) {
        noTargetMSHR = NULL;
        unblock = true;
        cause = Blocked_NoTargets;
    }
    if (mshrQueue.isFull()) {
        unblock = true;
        cause = Blocked_NoMSHRs;
    }
    mshrQueue.squash(threadNum);
    if (unblock && !mshrQueue.isFull()) {
        clearBlocked(cause);
    }
}

/////////////////////////////////////////////////////
//
// Access path: requests coming in from the CPU side
//
/////////////////////////////////////////////////////

template<class TagStore>
bool
Cache<TagStore>::access(PacketPtr pkt, BlkType *&blk,
                        int &lat, PacketList &writebacks)
{
    if (pkt->req->isUncacheable())  {
        blk = NULL;
        lat = hitLatency;
        return false;
    }

    int id = pkt->req->hasContextId() ? pkt->req->contextId() : -1;
    blk = tags->accessBlock(pkt->getAddr(), lat, id);

    DPRINTF(Cache, "%s%s %x %s\n", pkt->cmdString(),
            pkt->req->isInstFetch() ? " (ifetch)" : "",
            pkt->getAddr(), (blk) ? "hit" : "miss");

    if (blk != NULL) {

        if (pkt->needsExclusive() ? blk->isWritable() : blk->isReadable()) {
            // OK to satisfy access
            incHitCount(pkt, id);
            satisfyCpuSideRequest(pkt, blk);
            return true;
        }
    }

    // Can't satisfy access normally... either no block (blk == NULL)
    // or have block but need exclusive & only have shared.

    // Writeback handling is special case.  We can write the block
    // into the cache without having a writeable copy (or any copy at
    // all).
    if (pkt->cmd == MemCmd::Writeback) {
        assert(blkSize == pkt->getSize());
        if (blk == NULL) {
            // need to do a replacement
            blk = allocateBlock(pkt->getAddr(), writebacks);
            if (blk == NULL) {
                // no replaceable block available, give up.
                // writeback will be forwarded to next level.
                incMissCount(pkt, id);
                return false;
            }
            int id = pkt->req->hasContextId() ? pkt->req->contextId() : -1;
            tags->insertBlock(pkt->getAddr(), blk, id);
            blk->status = BlkValid | BlkReadable;
        }
        std::memcpy(blk->data, pkt->getPtr<uint8_t>(), blkSize);
        blk->status |= BlkDirty;
        // nothing else to do; writeback doesn't expect response
        assert(!pkt->needsResponse());
        incHitCount(pkt, id);
        return true;
    }

    incMissCount(pkt, id);

    if (blk == NULL && pkt->isLLSC() && pkt->isWrite()) {
        // complete miss on store conditional... just give up now
        pkt->req->setExtraData(0);
        return true;
    }

    return false;
}


class ForwardResponseRecord : public Packet::SenderState, public FastAlloc
{
    Packet::SenderState *prevSenderState;
    int prevSrc;
#ifndef NDEBUG
    BaseCache *cache;
#endif
  public:
    ForwardResponseRecord(Packet *pkt, BaseCache *_cache)
        : prevSenderState(pkt->senderState), prevSrc(pkt->getSrc())
#ifndef NDEBUG
          , cache(_cache)
#endif
    {}
    void restore(Packet *pkt, BaseCache *_cache)
    {
        assert(_cache == cache);
        pkt->senderState = prevSenderState;
        pkt->setDest(prevSrc);
    }
};


template<class TagStore>
bool
Cache<TagStore>::timingAccess(PacketPtr pkt)
{
//@todo Add back in MemDebug Calls
//    MemDebug::cacheAccess(pkt);

    // we charge hitLatency for doing just about anything here
    Tick time =  curTick + hitLatency;

    if (pkt->isResponse()) {
        // must be cache-to-cache response from upper to lower level
        ForwardResponseRecord *rec =
            dynamic_cast<ForwardResponseRecord *>(pkt->senderState);
        assert(rec != NULL);
        rec->restore(pkt, this);
        delete rec;
        memSidePort->respond(pkt, time);
        return true;
    }

    assert(pkt->isRequest());

    if (pkt->memInhibitAsserted()) {
        DPRINTF(Cache, "mem inhibited on 0x%x: not responding\n",
                pkt->getAddr());
        assert(!pkt->req->isUncacheable());
        // Special tweak for multilevel coherence: snoop downward here
        // on invalidates since there may be other caches below here
        // that have shared copies.  Not necessary if we know that
        // supplier had exclusive copy to begin with.
        if (pkt->needsExclusive() && !pkt->isSupplyExclusive()) {
            Packet *snoopPkt = new Packet(pkt, true);  // clear flags
            snoopPkt->setExpressSnoop();
            snoopPkt->assertMemInhibit();
            memSidePort->sendTiming(snoopPkt);
            // main memory will delete snoopPkt
        }
        // since we're the official target but we aren't responding,
        // delete the packet now.
        delete pkt;
        return true;
    }

    if (pkt->req->isUncacheable()) {
        // writes go in write buffer, reads use MSHR
        if (pkt->isWrite() && !pkt->isRead()) {
            allocateWriteBuffer(pkt, time, true);
        } else {
            allocateUncachedReadBuffer(pkt, time, true);
        }
        assert(pkt->needsResponse()); // else we should delete it here??
        return true;
    }

    int lat = hitLatency;
    BlkType *blk = NULL;
    PacketList writebacks;

    bool satisfied = access(pkt, blk, lat, writebacks);

#if 0
    /** @todo make the fast write alloc (wh64) work with coherence. */

    // If this is a block size write/hint (WH64) allocate the block here
    // if the coherence protocol allows it.
    if (!blk && pkt->getSize() >= blkSize && coherence->allowFastWrites() &&
        (pkt->cmd == MemCmd::WriteReq
         || pkt->cmd == MemCmd::WriteInvalidateReq) ) {
        // not outstanding misses, can do this
        MSHR *outstanding_miss = mshrQueue.findMatch(pkt->getAddr());
        if (pkt->cmd == MemCmd::WriteInvalidateReq || !outstanding_miss) {
            if (outstanding_miss) {
                warn("WriteInv doing a fastallocate"
                     "with an outstanding miss to the same address\n");
            }
            blk = handleFill(NULL, pkt, BlkValid | BlkWritable,
                                   writebacks);
            ++fastWrites;
        }
    }
#endif

    // track time of availability of next prefetch, if any
    Tick next_pf_time = 0;

    bool needsResponse = pkt->needsResponse();

    if (satisfied) {
        if (prefetcher && (prefetchOnAccess || (blk && blk->wasPrefetched()))) {
            next_pf_time = prefetcher->notify(pkt, time);
            if (blk && blk->wasPrefetched()) {
                blk->status &= ~BlkHWPrefetched;
                prefetchHits++;
            }
        }

        if (needsResponse) {
            pkt->makeTimingResponse();
            cpuSidePort->respond(pkt, curTick+lat);
        } else {
            delete pkt;
        }
    } else {
        // miss

        Addr blk_addr = blockAlign(pkt->getAddr());
        MSHR *mshr = mshrQueue.findMatch(blk_addr);

        if (mshr) {
            // MSHR hit
            //@todo remove hw_pf here
            mshr_hits[pkt->cmdToIndex()][0/*pkt->req->threadId()*/]++;
            if (mshr->threadNum != 0/*pkt->req->threadId()*/) {
                mshr->threadNum = -1;
            }
            mshr->allocateTarget(pkt, time, order++);
            if (mshr->getNumTargets() == numTarget) {
                noTargetMSHR = mshr;
                setBlocked(Blocked_NoTargets);
                // need to be careful with this... if this mshr isn't
                // ready yet (i.e. time > curTick_, we don't want to
                // move it ahead of mshrs that are ready
                // mshrQueue.moveToFront(mshr);
            }
        } else {
            // no MSHR
            mshr_misses[pkt->cmdToIndex()][0/*pkt->req->threadId()*/]++;
            // always mark as cache fill for now... if we implement
            // no-write-allocate or bypass accesses this will have to
            // be changed.
            if (pkt->cmd == MemCmd::Writeback) {
                allocateWriteBuffer(pkt, time, true);
            } else {
                if (blk && blk->isValid()) {
                    // If we have a write miss to a valid block, we
                    // need to mark the block non-readable.  Otherwise
                    // if we allow reads while there's an outstanding
                    // write miss, the read could return stale data
                    // out of the cache block... a more aggressive
                    // system could detect the overlap (if any) and
                    // forward data out of the MSHRs, but we don't do
                    // that yet.  Note that we do need to leave the
                    // block valid so that it stays in the cache, in
                    // case we get an upgrade response (and hence no
                    // new data) when the write miss completes.
                    // As long as CPUs do proper store/load forwarding
                    // internally, and have a sufficiently weak memory
                    // model, this is probably unnecessary, but at some
                    // point it must have seemed like we needed it...
                    assert(pkt->needsExclusive() && !blk->isWritable());
                    blk->status &= ~BlkReadable;
                }

                allocateMissBuffer(pkt, time, true);
            }

            if (prefetcher) {
                next_pf_time = prefetcher->notify(pkt, time);
            }
        }
    }

    if (next_pf_time != 0)
        requestMemSideBus(Request_PF, std::max(time, next_pf_time));

    // copy writebacks to write buffer
    while (!writebacks.empty()) {
        PacketPtr wbPkt = writebacks.front();
        allocateWriteBuffer(wbPkt, time, true);
        writebacks.pop_front();
    }

    return true;
}


// See comment in cache.hh.
template<class TagStore>
PacketPtr
Cache<TagStore>::getBusPacket(PacketPtr cpu_pkt, BlkType *blk,
                              bool needsExclusive)
{
    bool blkValid = blk && blk->isValid();

    if (cpu_pkt->req->isUncacheable()) {
        assert(blk == NULL);
        return NULL;
    }

    if (!blkValid &&
        (cpu_pkt->cmd == MemCmd::Writeback || cpu_pkt->isUpgrade())) {
        // Writebacks that weren't allocated in access() and upgrades
        // from upper-level caches that missed completely just go
        // through.
        return NULL;
    }

    assert(cpu_pkt->needsResponse());

    MemCmd cmd;
    // @TODO make useUpgrades a parameter.
    // Note that ownership protocols require upgrade, otherwise a
    // write miss on a shared owned block will generate a ReadExcl,
    // which will clobber the owned copy.
    const bool useUpgrades = true;
    if (blkValid && useUpgrades) {
        // only reason to be here is that blk is shared
        // (read-only) and we need exclusive
        assert(needsExclusive && !blk->isWritable());
        cmd = cpu_pkt->isLLSC() ? MemCmd::SCUpgradeReq : MemCmd::UpgradeReq;
    } else {
        // block is invalid
        cmd = needsExclusive ? MemCmd::ReadExReq : MemCmd::ReadReq;
    }
    PacketPtr pkt = new Packet(cpu_pkt->req, cmd, Packet::Broadcast, blkSize);

    pkt->allocate();
    return pkt;
}


template<class TagStore>
Tick
Cache<TagStore>::atomicAccess(PacketPtr pkt)
{
    int lat = hitLatency;

    // @TODO: make this a parameter
    bool last_level_cache = false;

    if (pkt->memInhibitAsserted()) {
        assert(!pkt->req->isUncacheable());
        // have to invalidate ourselves and any lower caches even if
        // upper cache will be responding
        if (pkt->isInvalidate()) {
            BlkType *blk = tags->findBlock(pkt->getAddr());
            if (blk && blk->isValid()) {
                tags->invalidateBlk(blk);
                DPRINTF(Cache, "rcvd mem-inhibited %s on 0x%x: invalidating\n",
                        pkt->cmdString(), pkt->getAddr());
            }
            if (!last_level_cache) {
                DPRINTF(Cache, "forwarding mem-inhibited %s on 0x%x\n",
                        pkt->cmdString(), pkt->getAddr());
                lat += memSidePort->sendAtomic(pkt);
            }
        } else {
            DPRINTF(Cache, "rcvd mem-inhibited %s on 0x%x: not responding\n",
                    pkt->cmdString(), pkt->getAddr());
        }

        return lat;
    }

    // should assert here that there are no outstanding MSHRs or
    // writebacks... that would mean that someone used an atomic
    // access in timing mode

    BlkType *blk = NULL;
    PacketList writebacks;

    if (!access(pkt, blk, lat, writebacks)) {
        // MISS
        PacketPtr bus_pkt = getBusPacket(pkt, blk, pkt->needsExclusive());

        bool is_forward = (bus_pkt == NULL);

        if (is_forward) {
            // just forwarding the same request to the next level
            // no local cache operation involved
            bus_pkt = pkt;
        }

        DPRINTF(Cache, "Sending an atomic %s for %x\n",
                bus_pkt->cmdString(), bus_pkt->getAddr());

#if TRACING_ON
        CacheBlk::State old_state = blk ? blk->status : 0;
#endif

        lat += memSidePort->sendAtomic(bus_pkt);

        DPRINTF(Cache, "Receive response: %s for addr %x in state %i\n",
                bus_pkt->cmdString(), bus_pkt->getAddr(), old_state);

        assert(!bus_pkt->wasNacked());

        // If packet was a forward, the response (if any) is already
        // in place in the bus_pkt == pkt structure, so we don't need
        // to do anything.  Otherwise, use the separate bus_pkt to
        // generate response to pkt and then delete it.
        if (!is_forward) {
            if (pkt->needsResponse()) {
                assert(bus_pkt->isResponse());
                if (bus_pkt->isError()) {
                    pkt->makeAtomicResponse();
                    pkt->copyError(bus_pkt);
                } else if (bus_pkt->isRead() ||
                           bus_pkt->cmd == MemCmd::UpgradeResp) {
                    // we're updating cache state to allow us to
                    // satisfy the upstream request from the cache
                    blk = handleFill(bus_pkt, blk, writebacks);
                    satisfyCpuSideRequest(pkt, blk);
                } else {
                    // we're satisfying the upstream request without
                    // modifying cache state, e.g., a write-through
                    pkt->makeAtomicResponse();
                }
            }
            delete bus_pkt;
        }
    }

    // Note that we don't invoke the prefetcher at all in atomic mode.
    // It's not clear how to do it properly, particularly for
    // prefetchers that aggressively generate prefetch candidates and
    // rely on bandwidth contention to throttle them; these will tend
    // to pollute the cache in atomic mode since there is no bandwidth
    // contention.  If we ever do want to enable prefetching in atomic
    // mode, though, this is the place to do it... see timingAccess()
    // for an example (though we'd want to issue the prefetch(es)
    // immediately rather than calling requestMemSideBus() as we do
    // there).

    // Handle writebacks if needed
    while (!writebacks.empty()){
        PacketPtr wbPkt = writebacks.front();
        memSidePort->sendAtomic(wbPkt);
        writebacks.pop_front();
        delete wbPkt;
    }

    // We now have the block one way or another (hit or completed miss)

    if (pkt->needsResponse()) {
        pkt->makeAtomicResponse();
    }

    return lat;
}


template<class TagStore>
void
Cache<TagStore>::functionalAccess(PacketPtr pkt,
                                  CachePort *incomingPort,
                                  CachePort *otherSidePort)
{
    Addr blk_addr = blockAlign(pkt->getAddr());
    BlkType *blk = tags->findBlock(pkt->getAddr());

    pkt->pushLabel(name());

    CacheBlkPrintWrapper cbpw(blk);
    bool done =
        (blk && pkt->checkFunctional(&cbpw, blk_addr, blkSize, blk->data))
        || incomingPort->checkFunctional(pkt)
        || mshrQueue.checkFunctional(pkt, blk_addr)
        || writeBuffer.checkFunctional(pkt, blk_addr)
        || otherSidePort->checkFunctional(pkt);

    // We're leaving the cache, so pop cache->name() label
    pkt->popLabel();

    if (!done) {
        otherSidePort->sendFunctional(pkt);
    }
}


/////////////////////////////////////////////////////
//
// Response handling: responses from the memory side
//
/////////////////////////////////////////////////////


template<class TagStore>
void
Cache<TagStore>::handleResponse(PacketPtr pkt)
{
    Tick time = curTick + hitLatency;
    MSHR *mshr = dynamic_cast<MSHR*>(pkt->senderState);
    bool is_error = pkt->isError();

    assert(mshr);

    if (pkt->wasNacked()) {
        //pkt->reinitFromRequest();
        warn("NACKs from devices not connected to the same bus "
             "not implemented\n");
        return;
    }
    if (is_error) {
        DPRINTF(Cache, "Cache received packet with error for address %x, "
                "cmd: %s\n", pkt->getAddr(), pkt->cmdString());
    }

    DPRINTF(Cache, "Handling response to %x\n", pkt->getAddr());

    MSHRQueue *mq = mshr->queue;
    bool wasFull = mq->isFull();

    if (mshr == noTargetMSHR) {
        // we always clear at least one target
        clearBlocked(Blocked_NoTargets);
        noTargetMSHR = NULL;
    }

    // Initial target is used just for stats
    MSHR::Target *initial_tgt = mshr->getTarget();
    BlkType *blk = tags->findBlock(pkt->getAddr());
    int stats_cmd_idx = initial_tgt->pkt->cmdToIndex();
    Tick miss_latency = curTick - initial_tgt->recvTime;
    PacketList writebacks;

    if (pkt->req->isUncacheable()) {
        mshr_uncacheable_lat[stats_cmd_idx][0/*pkt->req->threadId()*/] +=
            miss_latency;
    } else {
        mshr_miss_latency[stats_cmd_idx][0/*pkt->req->threadId()*/] +=
            miss_latency;
    }

    bool is_fill = !mshr->isForward &&
        (pkt->isRead() || pkt->cmd == MemCmd::UpgradeResp);

    if (is_fill && !is_error) {
        DPRINTF(Cache, "Block for addr %x being updated in Cache\n",
                pkt->getAddr());

        // give mshr a chance to do some dirty work
        mshr->handleFill(pkt, blk);

        blk = handleFill(pkt, blk, writebacks);
        assert(blk != NULL);
    }

    // First offset for critical word first calculations
    int initial_offset = 0;

    if (mshr->hasTargets()) {
        initial_offset = mshr->getTarget()->pkt->getOffset(blkSize);
    }

    while (mshr->hasTargets()) {
        MSHR::Target *target = mshr->getTarget();

        switch (target->source) {
          case MSHR::Target::FromCPU:
            Tick completion_time;
            if (is_fill) {
                satisfyCpuSideRequest(target->pkt, blk);
                // How many bytes past the first request is this one
                int transfer_offset =
                    target->pkt->getOffset(blkSize) - initial_offset;
                if (transfer_offset < 0) {
                    transfer_offset += blkSize;
                }

                // If critical word (no offset) return first word time
                completion_time = tags->getHitLatency() +
                    (transfer_offset ? pkt->finishTime : pkt->firstWordTime);

                assert(!target->pkt->req->isUncacheable());
                missLatency[target->pkt->cmdToIndex()][0/*pkt->req->threadId()*/] +=
                    completion_time - target->recvTime;
            } else if (target->pkt->cmd == MemCmd::StoreCondReq &&
                       pkt->cmd == MemCmd::UpgradeFailResp) {
                // failed StoreCond upgrade
                completion_time = tags->getHitLatency() + pkt->finishTime;
                target->pkt->req->setExtraData(0);
            } else {
                // not a cache fill, just forwarding response
                completion_time = tags->getHitLatency() + pkt->finishTime;
                if (pkt->isRead() && !is_error) {
                    target->pkt->setData(pkt->getPtr<uint8_t>());
                }
            }
            target->pkt->makeTimingResponse();
            // if this packet is an error copy that to the new packet
            if (is_error)
                target->pkt->copyError(pkt);
            if (pkt->isInvalidate()) {
                // If intermediate cache got ReadRespWithInvalidate,
                // propagate that.  Response should not have
                // isInvalidate() set otherwise.
                assert(target->pkt->cmd == MemCmd::ReadResp);
                assert(pkt->cmd == MemCmd::ReadRespWithInvalidate);
                target->pkt->cmd = MemCmd::ReadRespWithInvalidate;
            }
            cpuSidePort->respond(target->pkt, completion_time);
            break;

          case MSHR::Target::FromPrefetcher:
            assert(target->pkt->cmd == MemCmd::HardPFReq);
            if (blk) {
                blk->status |= BlkHWPrefetched;
                prefetcher->prefetchComplete(target->pkt);
            }
            delete target->pkt->req;
            delete target->pkt;
            break;

          case MSHR::Target::FromSnoop:
            // I don't believe that a snoop can be in an error state
            assert(!is_error);
            // response to snoop request
            DPRINTF(Cache, "processing deferred snoop...\n");
            handleSnoop(target->pkt, blk, true, true,
                        mshr->pendingInvalidate || pkt->isInvalidate());
            break;

          default:
            panic("Illegal target->source enum %d\n", target->source);
        }

        mshr->popTarget();
    }

    if (pkt->isInvalidate()) {
        tags->invalidateBlk(blk);
    }

    if (mshr->promoteDeferredTargets()) {
        // avoid later read getting stale data while write miss is
        // outstanding.. see comment in timingAccess()
        blk->status &= ~BlkReadable;
        MSHRQueue *mq = mshr->queue;
        mq->markPending(mshr);
        requestMemSideBus((RequestCause)mq->index, pkt->finishTime);
    } else {
        mq->deallocate(mshr);
        if (wasFull && !mq->isFull()) {
            clearBlocked((BlockedCause)mq->index);
        }
    }

    // copy writebacks to write buffer
    while (!writebacks.empty()) {
        PacketPtr wbPkt = writebacks.front();
        allocateWriteBuffer(wbPkt, time, true);
        writebacks.pop_front();
    }
    // if we used temp block, clear it out
    if (blk == tempBlock) {
        if (blk->isDirty()) {
            allocateWriteBuffer(writebackBlk(blk), time, true);
        }
        tags->invalidateBlk(blk);
    }

    delete pkt;
}




template<class TagStore>
PacketPtr
Cache<TagStore>::writebackBlk(BlkType *blk)
{
    assert(blk && blk->isValid() && blk->isDirty());

    writebacks[0/*pkt->req->threadId()*/]++;

    Request *writebackReq =
        new Request(tags->regenerateBlkAddr(blk->tag, blk->set), blkSize, 0);
    PacketPtr writeback = new Packet(writebackReq, MemCmd::Writeback, -1);
    writeback->allocate();
    std::memcpy(writeback->getPtr<uint8_t>(), blk->data, blkSize);

    blk->status &= ~BlkDirty;
    return writeback;
}


template<class TagStore>
typename Cache<TagStore>::BlkType*
Cache<TagStore>::allocateBlock(Addr addr, PacketList &writebacks)
{
    BlkType *blk = tags->findVictim(addr, writebacks);

    if (blk->isValid()) {
        Addr repl_addr = tags->regenerateBlkAddr(blk->tag, blk->set);
        MSHR *repl_mshr = mshrQueue.findMatch(repl_addr);
        if (repl_mshr) {
            // must be an outstanding upgrade request on block
            // we're about to replace...
            assert(!blk->isWritable());
            assert(repl_mshr->needsExclusive());
            // too hard to replace block with transient state
            // allocation failed, block not inserted
            return NULL;
        } else {
            DPRINTF(Cache, "replacement: replacing %x with %x: %s\n",
                    repl_addr, addr,
                    blk->isDirty() ? "writeback" : "clean");

            if (blk->isDirty()) {
                // Save writeback packet for handling by caller
                writebacks.push_back(writebackBlk(blk));
            }
        }
    }

    return blk;
}


// Note that the reason we return a list of writebacks rather than
// inserting them directly in the write buffer is that this function
// is called by both atomic and timing-mode accesses, and in atomic
// mode we don't mess with the write buffer (we just perform the
// writebacks atomically once the original request is complete).
template<class TagStore>
typename Cache<TagStore>::BlkType*
Cache<TagStore>::handleFill(PacketPtr pkt, BlkType *blk,
                            PacketList &writebacks)
{
    Addr addr = pkt->getAddr();
#if TRACING_ON
    CacheBlk::State old_state = blk ? blk->status : 0;
#endif

    if (blk == NULL) {
        // better have read new data...
        assert(pkt->hasData());
        // need to do a replacement
        blk = allocateBlock(addr, writebacks);
        if (blk == NULL) {
            // No replaceable block... just use temporary storage to
            // complete the current request and then get rid of it
            assert(!tempBlock->isValid());
            blk = tempBlock;
            tempBlock->set = tags->extractSet(addr);
            tempBlock->tag = tags->extractTag(addr);
            DPRINTF(Cache, "using temp block for %x\n", addr);
        } else {
            int id = pkt->req->hasContextId() ? pkt->req->contextId() : -1;
            tags->insertBlock(pkt->getAddr(), blk, id);
        }
    } else {
        // existing block... probably an upgrade
        assert(blk->tag == tags->extractTag(addr));
        // either we're getting new data or the block should already be valid
        assert(pkt->hasData() || blk->isValid());
    }

    blk->status = BlkValid | BlkReadable;

    if (!pkt->sharedAsserted()) {
        blk->status |= BlkWritable;
        // If we got this via cache-to-cache transfer (i.e., from a
        // cache that was an owner) and took away that owner's copy,
        // then we need to write it back.  Normally this happens
        // anyway as a side effect of getting a copy to write it, but
        // there are cases (such as failed store conditionals or
        // compare-and-swaps) where we'll demand an exclusive copy but
        // end up not writing it.
        if (pkt->memInhibitAsserted())
            blk->status |= BlkDirty;
    }

    DPRINTF(Cache, "Block addr %x moving from state %i to %i\n",
            addr, old_state, blk->status);

    // if we got new data, copy it in
    if (pkt->isRead()) {
        std::memcpy(blk->data, pkt->getPtr<uint8_t>(), blkSize);
    }

    blk->whenReady = pkt->finishTime;

    return blk;
}


/////////////////////////////////////////////////////
//
// Snoop path: requests coming in from the memory side
//
/////////////////////////////////////////////////////

template<class TagStore>
void
Cache<TagStore>::
doTimingSupplyResponse(PacketPtr req_pkt, uint8_t *blk_data,
                       bool already_copied, bool pending_inval)
{
    // timing-mode snoop responses require a new packet, unless we
    // already made a copy...
    PacketPtr pkt = already_copied ? req_pkt : new Packet(req_pkt);
    assert(req_pkt->isInvalidate() || pkt->sharedAsserted());
    pkt->allocate();
    pkt->makeTimingResponse();
    if (pkt->isRead()) {
        pkt->setDataFromBlock(blk_data, blkSize);
    }
    if (pkt->cmd == MemCmd::ReadResp && pending_inval) {
        // Assume we defer a response to a read from a far-away cache
        // A, then later defer a ReadExcl from a cache B on the same
        // bus as us.  We'll assert MemInhibit in both cases, but in
        // the latter case MemInhibit will keep the invalidation from
        // reaching cache A.  This special response tells cache A that
        // it gets the block to satisfy its read, but must immediately
        // invalidate it.
        pkt->cmd = MemCmd::ReadRespWithInvalidate;
    }
    memSidePort->respond(pkt, curTick + hitLatency);
}

template<class TagStore>
void
Cache<TagStore>::handleSnoop(PacketPtr pkt, BlkType *blk,
                             bool is_timing, bool is_deferred,
                             bool pending_inval)
{
    // deferred snoops can only happen in timing mode
    assert(!(is_deferred && !is_timing));
    // pending_inval only makes sense on deferred snoops
    assert(!(pending_inval && !is_deferred));
    assert(pkt->isRequest());

    if (forwardSnoops) {
        // first propagate snoop upward to see if anyone above us wants to
        // handle it.  save & restore packet src since it will get
        // rewritten to be relative to cpu-side bus (if any)
        bool alreadyResponded = pkt->memInhibitAsserted();
        if (is_timing) {
            Packet *snoopPkt = new Packet(pkt, true);  // clear flags
            snoopPkt->setExpressSnoop();
            snoopPkt->senderState = new ForwardResponseRecord(pkt, this);
            cpuSidePort->sendTiming(snoopPkt);
            if (snoopPkt->memInhibitAsserted()) {
                // cache-to-cache response from some upper cache
                assert(!alreadyResponded);
                pkt->assertMemInhibit();
            } else {
                delete snoopPkt->senderState;
            }
            if (snoopPkt->sharedAsserted()) {
                pkt->assertShared();
            }
            delete snoopPkt;
        } else {
            int origSrc = pkt->getSrc();
            cpuSidePort->sendAtomic(pkt);
            if (!alreadyResponded && pkt->memInhibitAsserted()) {
                // cache-to-cache response from some upper cache:
                // forward response to original requester
                assert(pkt->isResponse());
            }
            pkt->setSrc(origSrc);
        }
    }

    if (!blk || !blk->isValid()) {
        return;
    }

    // we may end up modifying both the block state and the packet (if
    // we respond in atomic mode), so just figure out what to do now
    // and then do it later
    bool respond = blk->isDirty() && pkt->needsResponse();
    bool have_exclusive = blk->isWritable();
    bool invalidate = pkt->isInvalidate();

    if (pkt->isRead() && !pkt->isInvalidate()) {
        assert(!pkt->needsExclusive());
        pkt->assertShared();
        int bits_to_clear = BlkWritable;
        const bool haveOwnershipState = true; // for now
        if (!haveOwnershipState) {
            // if we don't support pure ownership (dirty && !writable),
            // have to clear dirty bit here, assume memory snarfs data
            // on cache-to-cache xfer
            bits_to_clear |= BlkDirty;
        }
        blk->status &= ~bits_to_clear;
    }

    DPRINTF(Cache, "snooped a %s request for addr %x, %snew state is %i\n",
            pkt->cmdString(), blockAlign(pkt->getAddr()),
            respond ? "responding, " : "", invalidate ? 0 : blk->status);

    if (respond) {
        assert(!pkt->memInhibitAsserted());
        pkt->assertMemInhibit();
        if (have_exclusive) {
            pkt->setSupplyExclusive();
        }
        if (is_timing) {
            doTimingSupplyResponse(pkt, blk->data, is_deferred, pending_inval);
        } else {
            pkt->makeAtomicResponse();
            pkt->setDataFromBlock(blk->data, blkSize);
        }
    } else if (is_timing && is_deferred) {
        // if it's a deferred timing snoop then we've made a copy of
        // the packet, and so if we're not using that copy to respond
        // then we need to delete it here.
        delete pkt;
    }

    // Do this last in case it deallocates block data or something
    // like that
    if (invalidate) {
        tags->invalidateBlk(blk);
    }
}


template<class TagStore>
void
Cache<TagStore>::snoopTiming(PacketPtr pkt)
{
    // Note that some deferred snoops don't have requests, since the
    // original access may have already completed
    if ((pkt->req && pkt->req->isUncacheable()) ||
        pkt->cmd == MemCmd::Writeback) {
        //Can't get a hit on an uncacheable address
        //Revisit this for multi level coherence
        return;
    }

    BlkType *blk = tags->findBlock(pkt->getAddr());

    Addr blk_addr = blockAlign(pkt->getAddr());
    MSHR *mshr = mshrQueue.findMatch(blk_addr);

    // Let the MSHR itself track the snoop and decide whether we want
    // to go ahead and do the regular cache snoop
    if (mshr && mshr->handleSnoop(pkt, order++)) {
        DPRINTF(Cache, "Deferring snoop on in-service MSHR to blk %x\n",
                blk_addr);
        if (mshr->getNumTargets() > numTarget)
            warn("allocating bonus target for snoop"); //handle later
        return;
    }

    //We also need to check the writeback buffers and handle those
    std::vector<MSHR *> writebacks;
    if (writeBuffer.findMatches(blk_addr, writebacks)) {
        DPRINTF(Cache, "Snoop hit in writeback to addr: %x\n",
                pkt->getAddr());

        //Look through writebacks for any non-uncachable writes, use that
        for (int i = 0; i < writebacks.size(); i++) {
            mshr = writebacks[i];
            assert(!mshr->isUncacheable());
            assert(mshr->getNumTargets() == 1);
            PacketPtr wb_pkt = mshr->getTarget()->pkt;
            assert(wb_pkt->cmd == MemCmd::Writeback);

            assert(!pkt->memInhibitAsserted());
            pkt->assertMemInhibit();
            if (!pkt->needsExclusive()) {
                pkt->assertShared();
            } else {
                // if we're not asserting the shared line, we need to
                // invalidate our copy.  we'll do that below as long as
                // the packet's invalidate flag is set...
                assert(pkt->isInvalidate());
            }
            doTimingSupplyResponse(pkt, wb_pkt->getPtr<uint8_t>(),
                                   false, false);

            if (pkt->isInvalidate()) {
                // Invalidation trumps our writeback... discard here
                markInService(mshr);
                delete wb_pkt;
            }

            // If this was a shared writeback, there may still be
            // other shared copies above that require invalidation.
            // We could be more selective and return here if the
            // request is non-exclusive or if the writeback is
            // exclusive.
            break;
        }
    }

    handleSnoop(pkt, blk, true, false, false);
}


template<class TagStore>
Tick
Cache<TagStore>::snoopAtomic(PacketPtr pkt)
{
    if (pkt->req->isUncacheable() || pkt->cmd == MemCmd::Writeback) {
        // Can't get a hit on an uncacheable address
        // Revisit this for multi level coherence
        return hitLatency;
    }

    BlkType *blk = tags->findBlock(pkt->getAddr());
    handleSnoop(pkt, blk, false, false, false);
    return hitLatency;
}


template<class TagStore>
MSHR *
Cache<TagStore>::getNextMSHR()
{
    // Check both MSHR queue and write buffer for potential requests
    MSHR *miss_mshr  = mshrQueue.getNextMSHR();
    MSHR *write_mshr = writeBuffer.getNextMSHR();

    // Now figure out which one to send... some cases are easy
    if (miss_mshr && !write_mshr) {
        return miss_mshr;
    }
    if (write_mshr && !miss_mshr) {
        return write_mshr;
    }

    if (miss_mshr && write_mshr) {
        // We have one of each... normally we favor the miss request
        // unless the write buffer is full
        if (writeBuffer.isFull() && writeBuffer.inServiceEntries == 0) {
            // Write buffer is full, so we'd like to issue a write;
            // need to search MSHR queue for conflicting earlier miss.
            MSHR *conflict_mshr =
                mshrQueue.findPending(write_mshr->addr, write_mshr->size);

            if (conflict_mshr && conflict_mshr->order < write_mshr->order) {
                // Service misses in order until conflict is cleared.
                return conflict_mshr;
            }

            // No conflicts; issue write
            return write_mshr;
        }

        // Write buffer isn't full, but need to check it for
        // conflicting earlier writeback
        MSHR *conflict_mshr =
            writeBuffer.findPending(miss_mshr->addr, miss_mshr->size);
        if (conflict_mshr) {
            // not sure why we don't check order here... it was in the
            // original code but commented out.

            // The only way this happens is if we are
            // doing a write and we didn't have permissions
            // then subsequently saw a writeback (owned got evicted)
            // We need to make sure to perform the writeback first
            // To preserve the dirty data, then we can issue the write

            // should we return write_mshr here instead?  I.e. do we
            // have to flush writes in order?  I don't think so... not
            // for Alpha anyway.  Maybe for x86?
            return conflict_mshr;
        }

        // No conflicts; issue read
        return miss_mshr;
    }

    // fall through... no pending requests.  Try a prefetch.
    assert(!miss_mshr && !write_mshr);
    if (prefetcher && !mshrQueue.isFull()) {
        // If we have a miss queue slot, we can try a prefetch
        PacketPtr pkt;
        while (prefetcher->nextPrefetchReadyTime() <= curTick && (pkt = prefetcher->getPacket()) != NULL) {
            Addr pf_addr = blockAlign(pkt->getAddr());
            if (!tags->findBlock(pf_addr) && !mshrQueue.findMatch(pf_addr)) {
                // Update statistic on number of prefetches issued
                // (hwpf_mshr_misses)
                mshr_misses[pkt->cmdToIndex()][0/*pkt->req->threadId()*/]++;
                // Don't request bus, since we already have it
                return allocateMissBuffer(pkt, curTick, false);
            } else {
                delete pkt->req;
                delete pkt;
            }
        }
    }

    return NULL;
}


template<class TagStore>
PacketPtr
Cache<TagStore>::getTimingPacket()
{
    MSHR *mshr = getNextMSHR();

    if (mshr == NULL) {
        return NULL;
    }

    // use request from 1st target
    PacketPtr tgt_pkt = mshr->getTarget()->pkt;
    PacketPtr pkt = NULL;

    if (tgt_pkt->cmd == MemCmd::SCUpgradeFailReq) {
        // SCUpgradeReq saw invalidation while queued in MSHR, so now
        // that we are getting around to processing it, just treat it
        // as if we got a failure response
        pkt = new Packet(tgt_pkt);
        pkt->cmd = MemCmd::UpgradeFailResp;
        pkt->senderState = mshr;
        pkt->firstWordTime = pkt->finishTime = curTick;
        handleResponse(pkt);
        return NULL;
    } else if (mshr->isForwardNoResponse()) {
        // no response expected, just forward packet as it is
        assert(tags->findBlock(mshr->addr) == NULL);
        pkt = tgt_pkt;
    } else {
        BlkType *blk = tags->findBlock(mshr->addr);
        // Bail out if sending a prefetch request for something already in memory.
        if (blk && blk->isValid() && tgt_pkt->cmd == MemCmd::HardPFReq) {
            MSHRQueue *mq = mshr->queue;
            mshr->popTarget();
            if (!mshr->hasTargets())
                mq->deallocate(mshr);
            return NULL;
        }
        pkt = getBusPacket(tgt_pkt, blk, mshr->needsExclusive());

        mshr->isForward = (pkt == NULL);

        if (mshr->isForward) {
            // not a cache block request, but a response is expected
            // make copy of current packet to forward, keep current
            // copy for response handling
            pkt = new Packet(tgt_pkt);
            pkt->allocate();
            if (pkt->isWrite()) {
                pkt->setData(tgt_pkt->getPtr<uint8_t>());
            }
        }
    }

    assert(pkt != NULL);
    pkt->senderState = mshr;
    return pkt;
}


template<class TagStore>
Tick
Cache<TagStore>::nextMSHRReadyTime()
{
    Tick nextReady = std::min(mshrQueue.nextMSHRReadyTime(),
                              writeBuffer.nextMSHRReadyTime());

    if (prefetcher) {
        nextReady = std::min(nextReady,
                             prefetcher->nextPrefetchReadyTime());
    }

    return nextReady;
}


///////////////
//
// CpuSidePort
//
///////////////

template<class TagStore>
void
Cache<TagStore>::CpuSidePort::
getDeviceAddressRanges(AddrRangeList &resp, bool &snoop)
{
    // CPU side port doesn't snoop; it's a target only.  It can
    // potentially respond to any address.
    snoop = false;
    resp.push_back(myCache()->getAddrRange());
}


template<class TagStore>
bool
Cache<TagStore>::CpuSidePort::recvTiming(PacketPtr pkt)
{
    // illegal to block responses... can lead to deadlock
    if (pkt->isRequest() && !pkt->memInhibitAsserted() && blocked) {
        DPRINTF(Cache,"Scheduling a retry while blocked\n");
        mustSendRetry = true;
        return false;
    }

    myCache()->timingAccess(pkt);
    return true;
}


template<class TagStore>
Tick
Cache<TagStore>::CpuSidePort::recvAtomic(PacketPtr pkt)
{
    return myCache()->atomicAccess(pkt);
}


template<class TagStore>
void
Cache<TagStore>::CpuSidePort::recvFunctional(PacketPtr pkt)
{
    myCache()->functionalAccess(pkt, this, otherPort);
}


template<class TagStore>
Cache<TagStore>::
CpuSidePort::CpuSidePort(const std::string &_name, Cache<TagStore> *_cache,
                         const std::string &_label)
    : BaseCache::CachePort(_name, _cache, _label)
{
}

///////////////
//
// MemSidePort
//
///////////////

template<class TagStore>
void
Cache<TagStore>::MemSidePort::
getDeviceAddressRanges(AddrRangeList &resp, bool &snoop)
{
    // Memory-side port always snoops, but never passes requests
    // through to targets on the cpu side (so we don't add anything to
    // the address range list).
    snoop = true;
}


template<class TagStore>
bool
Cache<TagStore>::MemSidePort::recvTiming(PacketPtr pkt)
{
    // this needs to be fixed so that the cache updates the mshr and sends the
    // packet back out on the link, but it probably won't happen so until this
    // gets fixed, just panic when it does
    if (pkt->wasNacked())
        panic("Need to implement cache resending nacked packets!\n");

    if (pkt->isRequest() && blocked) {
        DPRINTF(Cache,"Scheduling a retry while blocked\n");
        mustSendRetry = true;
        return false;
    }

    if (pkt->isResponse()) {
        myCache()->handleResponse(pkt);
    } else {
        myCache()->snoopTiming(pkt);
    }
    return true;
}


template<class TagStore>
Tick
Cache<TagStore>::MemSidePort::recvAtomic(PacketPtr pkt)
{
    // in atomic mode, responses go back to the sender via the
    // function return from sendAtomic(), not via a separate
    // sendAtomic() from the responder.  Thus we should never see a
    // response packet in recvAtomic() (anywhere, not just here).
    assert(!pkt->isResponse());
    return myCache()->snoopAtomic(pkt);
}


template<class TagStore>
void
Cache<TagStore>::MemSidePort::recvFunctional(PacketPtr pkt)
{
    myCache()->functionalAccess(pkt, this, otherPort);
}



template<class TagStore>
void
Cache<TagStore>::MemSidePort::sendPacket()
{
    // if we have responses that are ready, they take precedence
    if (deferredPacketReady()) {
        bool success = sendTiming(transmitList.front().pkt);

        if (success) {
            //send successful, remove packet
            transmitList.pop_front();
        }

        waitingOnRetry = !success;
    } else {
        // check for non-response packets (requests & writebacks)
        PacketPtr pkt = myCache()->getTimingPacket();
        if (pkt == NULL) {
            // can happen if e.g. we attempt a writeback and fail, but
            // before the retry, the writeback is eliminated because
            // we snoop another cache's ReadEx.
            waitingOnRetry = false;
        } else {
            MSHR *mshr = dynamic_cast<MSHR*>(pkt->senderState);

            bool success = sendTiming(pkt);

            waitingOnRetry = !success;
            if (waitingOnRetry) {
                DPRINTF(CachePort, "now waiting on a retry\n");
                if (!mshr->isForwardNoResponse()) {
                    delete pkt;
                }
            } else {
                myCache()->markInService(mshr);
            }
        }
    }


    // tried to send packet... if it was successful (no retry), see if
    // we need to rerequest bus or not
    if (!waitingOnRetry) {
        Tick nextReady = std::min(deferredPacketReadyTime(),
                                  myCache()->nextMSHRReadyTime());
        // @TODO: need to facotr in prefetch requests here somehow
        if (nextReady != MaxTick) {
            DPRINTF(CachePort, "more packets to send @ %d\n", nextReady);
            schedule(sendEvent, std::max(nextReady, curTick + 1));
        } else {
            // no more to send right now: if we're draining, we may be done
            if (drainEvent && !sendEvent->scheduled()) {
                drainEvent->process();
                drainEvent = NULL;
            }
        }
    }
}

template<class TagStore>
void
Cache<TagStore>::MemSidePort::recvRetry()
{
    assert(waitingOnRetry);
    sendPacket();
}


template<class TagStore>
void
Cache<TagStore>::MemSidePort::processSendEvent()
{
    assert(!waitingOnRetry);
    sendPacket();
}


template<class TagStore>
Cache<TagStore>::
MemSidePort::MemSidePort(const std::string &_name, Cache<TagStore> *_cache,
                         const std::string &_label)
    : BaseCache::CachePort(_name, _cache, _label)
{
    // override default send event from SimpleTimingPort
    delete sendEvent;
    sendEvent = new SendEvent(this);
}
