/*
 * Copyright (c) 2005 The Regents of The University of Michigan
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
 * Authors: Ron Dreslinski
 */

/**
 * @file
 * Hardware Prefetcher Definition.
 */

#include <list>

#include "arch/isa_traits.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "mem/cache/base.hh"
#include "mem/cache/prefetch/base.hh"
#include "mem/request.hh"

BasePrefetcher::BasePrefetcher(const BaseCacheParams *p)
    : size(p->prefetcher_size), pageStop(!p->prefetch_past_page),
      serialSquash(p->prefetch_serial_squash),
      onlyData(p->prefetch_data_accesses_only)
{
}

void
BasePrefetcher::setCache(BaseCache *_cache)
{
    cache = _cache;
    blkSize = cache->getBlockSize();
    _name = cache->name() + "-pf";
}

void
BasePrefetcher::regStats(const std::string &name)
{
    pfIdentified
        .name(name + ".prefetcher.num_hwpf_identified")
        .desc("number of hwpf identified")
        ;

    pfBufferHit
        .name(name + ".prefetcher.num_hwpf_already_in_prefetcher")
        .desc("number of hwpf that were already in the prefetch queue")
        ;

    pfRemovedFull
        .name(name + ".prefetcher.num_hwpf_evicted")
        .desc("number of hwpf removed due to no buffer left")
        ;

    pfRemovedMSHR
        .name(name + ".prefetcher.num_hwpf_removed_MSHR_hit")
        .desc("number of hwpf removed because MSHR allocated")
        ;

    pfIssued
        .name(name + ".prefetcher.num_hwpf_issued")
        .desc("number of hwpf issued")
        ;

    pfSpanPage
        .name(name + ".prefetcher.num_hwpf_span_page")
        .desc("number of hwpf spanning a virtual page")
        ;

    pfSquashed
        .name(name + ".prefetcher.num_hwpf_squashed_from_miss")
        .desc("number of hwpf that got squashed due to a miss "
              "aborting calculation time")
        ;
}

bool
BasePrefetcher::getPrefetchBit(Addr addr)
{
    assert(cache != 0);
    return cache->getPrefetchBit(addr);
}

void
BasePrefetcher::setPrefetchBit(Addr addr)
{
    assert(cache != 0);
    cache->setPrefetchBit(addr);
}

void
BasePrefetcher::clearPrefetchBit(Addr addr)
{
    assert(cache != 0);
    cache->clearPrefetchBit(addr);
}

bool
BasePrefetcher::inCache(Addr addr)
{
    return cache->inCache(addr);
}

bool
BasePrefetcher::inMissQueue(Addr addr)
{
    return cache->inMissQueue(addr);
}

PacketPtr
BasePrefetcher::getPacket()
{
    DPRINTF(HWPrefetch, "Requesting a hw_pf to issue\n");

    if (pf.empty()) {
        DPRINTF(HWPrefetch, "No HW_PF found\n");
        return NULL;
    }

    PacketPtr pkt;
    pkt = *pf.begin();
    pf.pop_front();

    if (pf.empty()) {
        cache->deassertMemSideBusRequest(BaseCache::Request_PF);
    }

    pfIssued++;
    assert(pkt != NULL);
    DPRINTF(HWPrefetch, "returning 0x%x\n", pkt->getAddr());
    return pkt;
}


Tick
BasePrefetcher::notify(PacketPtr &pkt, Tick time)
{
    if (!pkt->req->isUncacheable() && !(pkt->req->isInstFetch() && onlyData)) {
        // Calculate the blk address
        Addr blk_addr = pkt->getAddr() & ~(Addr)(blkSize-1);

        // Check if miss is in pfq, if so remove it
        std::list<PacketPtr>::iterator iter = inPrefetch(blk_addr);
        if (iter != pf.end()) {
            DPRINTF(HWPrefetch, "Saw a miss to a queued prefetch addr: "
                    "0x%x, removing it\n", blk_addr);
            pfRemovedMSHR++;
            delete (*iter)->req;
            delete (*iter);
            pf.erase(iter);
            if (pf.empty())
                cache->deassertMemSideBusRequest(BaseCache::Request_PF);
        }

        // Remove anything in queue with delay older than time
        // since everything is inserted in time order, start from end
        // and work until pf.empty() or time is earlier
        // This is done to emulate Aborting the previous work on a new miss
        // Needed for serial calculators like GHB
        if (serialSquash) {
            iter = pf.end();
            iter--;
            while (!pf.empty() && ((*iter)->time >= time)) {
                pfSquashed++;
                DPRINTF(HWPrefetch, "Squashing old prefetch addr: 0x%x\n",
                        (*iter)->getAddr());
                delete (*iter)->req;
                delete (*iter);
                pf.erase(iter);
                iter--;
            }
            if (pf.empty())
                cache->deassertMemSideBusRequest(BaseCache::Request_PF);
        }


        std::list<Addr> addresses;
        std::list<Tick> delays;
        calculatePrefetch(pkt, addresses, delays);

        std::list<Addr>::iterator addrIter = addresses.begin();
        std::list<Tick>::iterator delayIter = delays.begin();
        for (; addrIter != addresses.end(); ++addrIter, ++delayIter) {
            Addr addr = *addrIter;

            pfIdentified++;

            DPRINTF(HWPrefetch, "Found a pf candidate addr: 0x%x, "
                    "inserting into prefetch queue with delay %d time %d\n",
                    addr, *delayIter, time);

            // Check if it is already in the pf buffer
            if (inPrefetch(addr) != pf.end()) {
                pfBufferHit++;
                DPRINTF(HWPrefetch, "Prefetch addr already in pf buffer\n");
                continue;
            }

            // create a prefetch memreq
            Request *prefetchReq = new Request(*addrIter, blkSize, 0);
            PacketPtr prefetch =
                new Packet(prefetchReq, MemCmd::HardPFReq, Packet::Broadcast);
            prefetch->allocate();
            assert(prefetch != 0);
            assert(prefetch->req != 0); /* ??? */
            if (pkt->req->hasContextId()) {
                prefetch->req->setThreadContext(pkt->req->contextId(),
                                                pkt->req->threadId());
            }

            prefetch->time = time + (*delayIter); // @todo ADD LATENCY HERE

            // We just remove the head if we are full
            if (pf.size() == size) {
                pfRemovedFull++;
                PacketPtr old_pkt = *pf.begin();
                DPRINTF(HWPrefetch, "Prefetch queue full, "
                        "removing oldest 0x%x\n", old_pkt->getAddr());
                delete old_pkt->req;
                delete old_pkt;
                pf.pop_front();
            }

            pf.push_back(prefetch);
        }
    }

    return pf.empty() ? 0 : pf.front()->time;
}

void
BasePrefetcher::prefetchComplete(PacketPtr &pkt)
{
    return;
}

std::list<PacketPtr>::iterator
BasePrefetcher::inPrefetch(Addr address)
{
    // Guaranteed to only be one match, we always check before inserting
    std::list<PacketPtr>::iterator iter;
    for (iter = pf.begin(); iter != pf.end(); iter++) {
        if (((*iter)->getAddr() & ~(Addr)(blkSize-1)) == address) {
            return iter;
        }
    }
    return pf.end();
}

bool
BasePrefetcher::samePage(Addr a, Addr b)
{
    return roundDown(a, TheISA::VMPageSize) == roundDown(b, TheISA::VMPageSize);
}
