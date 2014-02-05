/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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
 */

/**
 * @file
 * Definition of BaseCache functions.
 */

#include "cpu/base.hh"
#include "cpu/smt.hh"
#include "mem/cache/base.hh"
#include "mem/cache/mshr.hh"

using namespace std;

BaseCache::CachePort::CachePort(const std::string &_name, BaseCache *_cache,
                                const std::string &_label)
    : SimpleTimingPort(_name, _cache), cache(_cache),
      label(_label), otherPort(NULL),
      blocked(false), mustSendRetry(false)
{
}


BaseCache::BaseCache(const Params *p)
    : MemObject(p),
      mshrQueue("MSHRs", p->mshrs, 4, MSHRQueue_MSHRs),
      writeBuffer("write buffer", p->write_buffers, p->mshrs+1000,
                  MSHRQueue_WriteBuffer),
      blkSize(p->block_size),
      hitLatency(p->latency),
      numTarget(p->tgts_per_mshr),
      forwardSnoops(p->forward_snoops),
      blocked(0),
      noTargetMSHR(NULL),
      missCount(p->max_miss_count),
      drainEvent(NULL),
      addrRange(p->addr_range),
      _numCpus(p->num_cpus)
{
}

void
BaseCache::CachePort::recvStatusChange(Port::Status status)
{
    if (status == Port::RangeChange) {
        otherPort->sendStatusChange(Port::RangeChange);
    }
}


bool
BaseCache::CachePort::checkFunctional(PacketPtr pkt)
{
    pkt->pushLabel(label);
    bool done = SimpleTimingPort::checkFunctional(pkt);
    pkt->popLabel();
    return done;
}


unsigned
BaseCache::CachePort::deviceBlockSize() const
{
    return cache->getBlockSize();
}


bool
BaseCache::CachePort::recvRetryCommon()
{
    assert(waitingOnRetry);
    waitingOnRetry = false;
    return false;
}


void
BaseCache::CachePort::setBlocked()
{
    assert(!blocked);
    DPRINTF(Cache, "Cache Blocking\n");
    blocked = true;
    //Clear the retry flag
    mustSendRetry = false;
}

void
BaseCache::CachePort::clearBlocked()
{
    assert(blocked);
    DPRINTF(Cache, "Cache Unblocking\n");
    blocked = false;
    if (mustSendRetry)
    {
        DPRINTF(Cache, "Cache Sending Retry\n");
        mustSendRetry = false;
        SendRetryEvent *ev = new SendRetryEvent(this, true);
        // @TODO: need to find a better time (next bus cycle?)
        schedule(ev, curTick + 1);
    }
}


void
BaseCache::init()
{
    if (!cpuSidePort || !memSidePort)
        panic("Cache not hooked up on both sides\n");
    cpuSidePort->sendStatusChange(Port::RangeChange);
}


void
BaseCache::regStats()
{
    using namespace Stats;

    // Hit statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        hits[access_idx]
#if FULL_SYSTEM
            .init(_numCpus + 1)
#else
            .init(_numCpus)
#endif
            .name(name() + "." + cstr + "_hits")
            .desc("number of " + cstr + " hits")
            .flags(total | nozero | nonan)
            ;
    }

// These macros make it easier to sum the right subset of commands and
// to change the subset of commands that are considered "demand" vs
// "non-demand"
#define SUM_DEMAND(s) \
    (s[MemCmd::ReadReq] + s[MemCmd::WriteReq] + s[MemCmd::ReadExReq])

// should writebacks be included here?  prior code was inconsistent...
#define SUM_NON_DEMAND(s) \
    (s[MemCmd::SoftPFReq] + s[MemCmd::HardPFReq])

    demandHits
        .name(name() + ".demand_hits")
        .desc("number of demand (read+write) hits")
        .flags(total)
        ;
    demandHits = SUM_DEMAND(hits);

    overallHits
        .name(name() + ".overall_hits")
        .desc("number of overall hits")
        .flags(total)
        ;
    overallHits = demandHits + SUM_NON_DEMAND(hits);

    // Miss statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        misses[access_idx]
#if FULL_SYSTEM
            .init(_numCpus + 1)
#else
            .init(_numCpus)
#endif
            .name(name() + "." + cstr + "_misses")
            .desc("number of " + cstr + " misses")
            .flags(total | nozero | nonan)
            ;
    }

    demandMisses
        .name(name() + ".demand_misses")
        .desc("number of demand (read+write) misses")
        .flags(total)
        ;
    demandMisses = SUM_DEMAND(misses);

    overallMisses
        .name(name() + ".overall_misses")
        .desc("number of overall misses")
        .flags(total)
        ;
    overallMisses = demandMisses + SUM_NON_DEMAND(misses);

    // Miss latency statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        missLatency[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_miss_latency")
            .desc("number of " + cstr + " miss cycles")
            .flags(total | nozero | nonan)
            ;
    }

    demandMissLatency
        .name(name() + ".demand_miss_latency")
        .desc("number of demand (read+write) miss cycles")
        .flags(total)
        ;
    demandMissLatency = SUM_DEMAND(missLatency);

    overallMissLatency
        .name(name() + ".overall_miss_latency")
        .desc("number of overall miss cycles")
        .flags(total)
        ;
    overallMissLatency = demandMissLatency + SUM_NON_DEMAND(missLatency);

    // access formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        accesses[access_idx]
            .name(name() + "." + cstr + "_accesses")
            .desc("number of " + cstr + " accesses(hits+misses)")
            .flags(total | nozero | nonan)
            ;

        accesses[access_idx] = hits[access_idx] + misses[access_idx];
    }

    demandAccesses
        .name(name() + ".demand_accesses")
        .desc("number of demand (read+write) accesses")
        .flags(total)
        ;
    demandAccesses = demandHits + demandMisses;

    overallAccesses
        .name(name() + ".overall_accesses")
        .desc("number of overall (read+write) accesses")
        .flags(total)
        ;
    overallAccesses = overallHits + overallMisses;

    // miss rate formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        missRate[access_idx]
            .name(name() + "." + cstr + "_miss_rate")
            .desc("miss rate for " + cstr + " accesses")
            .flags(total | nozero | nonan)
            ;

        missRate[access_idx] = misses[access_idx] / accesses[access_idx];
    }

    demandMissRate
        .name(name() + ".demand_miss_rate")
        .desc("miss rate for demand accesses")
        .flags(total)
        ;
    demandMissRate = demandMisses / demandAccesses;

    overallMissRate
        .name(name() + ".overall_miss_rate")
        .desc("miss rate for overall accesses")
        .flags(total)
        ;
    overallMissRate = overallMisses / overallAccesses;

    // miss latency formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        avgMissLatency[access_idx]
            .name(name() + "." + cstr + "_avg_miss_latency")
            .desc("average " + cstr + " miss latency")
            .flags(total | nozero | nonan)
            ;

        avgMissLatency[access_idx] =
            missLatency[access_idx] / misses[access_idx];
    }

    demandAvgMissLatency
        .name(name() + ".demand_avg_miss_latency")
        .desc("average overall miss latency")
        .flags(total)
        ;
    demandAvgMissLatency = demandMissLatency / demandMisses;

    overallAvgMissLatency
        .name(name() + ".overall_avg_miss_latency")
        .desc("average overall miss latency")
        .flags(total)
        ;
    overallAvgMissLatency = overallMissLatency / overallMisses;

    blocked_cycles.init(NUM_BLOCKED_CAUSES);
    blocked_cycles
        .name(name() + ".blocked_cycles")
        .desc("number of cycles access was blocked")
        .subname(Blocked_NoMSHRs, "no_mshrs")
        .subname(Blocked_NoTargets, "no_targets")
        ;


    blocked_causes.init(NUM_BLOCKED_CAUSES);
    blocked_causes
        .name(name() + ".blocked")
        .desc("number of cycles access was blocked")
        .subname(Blocked_NoMSHRs, "no_mshrs")
        .subname(Blocked_NoTargets, "no_targets")
        ;

    avg_blocked
        .name(name() + ".avg_blocked_cycles")
        .desc("average number of cycles each access was blocked")
        .subname(Blocked_NoMSHRs, "no_mshrs")
        .subname(Blocked_NoTargets, "no_targets")
        ;

    avg_blocked = blocked_cycles / blocked_causes;

    fastWrites
        .name(name() + ".fast_writes")
        .desc("number of fast writes performed")
        ;

    cacheCopies
        .name(name() + ".cache_copies")
        .desc("number of cache copies performed")
        ;

    writebacks
        .init(maxThreadsPerCPU)
        .name(name() + ".writebacks")
        .desc("number of writebacks")
        .flags(total)
        ;

    // MSHR statistics
    // MSHR hit statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshr_hits[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_mshr_hits")
            .desc("number of " + cstr + " MSHR hits")
            .flags(total | nozero | nonan)
            ;
    }

    demandMshrHits
        .name(name() + ".demand_mshr_hits")
        .desc("number of demand (read+write) MSHR hits")
        .flags(total)
        ;
    demandMshrHits = SUM_DEMAND(mshr_hits);

    overallMshrHits
        .name(name() + ".overall_mshr_hits")
        .desc("number of overall MSHR hits")
        .flags(total)
        ;
    overallMshrHits = demandMshrHits + SUM_NON_DEMAND(mshr_hits);

    // MSHR miss statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshr_misses[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_mshr_misses")
            .desc("number of " + cstr + " MSHR misses")
            .flags(total | nozero | nonan)
            ;
    }

    demandMshrMisses
        .name(name() + ".demand_mshr_misses")
        .desc("number of demand (read+write) MSHR misses")
        .flags(total)
        ;
    demandMshrMisses = SUM_DEMAND(mshr_misses);

    overallMshrMisses
        .name(name() + ".overall_mshr_misses")
        .desc("number of overall MSHR misses")
        .flags(total)
        ;
    overallMshrMisses = demandMshrMisses + SUM_NON_DEMAND(mshr_misses);

    // MSHR miss latency statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshr_miss_latency[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_mshr_miss_latency")
            .desc("number of " + cstr + " MSHR miss cycles")
            .flags(total | nozero | nonan)
            ;
    }

    demandMshrMissLatency
        .name(name() + ".demand_mshr_miss_latency")
        .desc("number of demand (read+write) MSHR miss cycles")
        .flags(total)
        ;
    demandMshrMissLatency = SUM_DEMAND(mshr_miss_latency);

    overallMshrMissLatency
        .name(name() + ".overall_mshr_miss_latency")
        .desc("number of overall MSHR miss cycles")
        .flags(total)
        ;
    overallMshrMissLatency =
        demandMshrMissLatency + SUM_NON_DEMAND(mshr_miss_latency);

    // MSHR uncacheable statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshr_uncacheable[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_mshr_uncacheable")
            .desc("number of " + cstr + " MSHR uncacheable")
            .flags(total | nozero | nonan)
            ;
    }

    overallMshrUncacheable
        .name(name() + ".overall_mshr_uncacheable_misses")
        .desc("number of overall MSHR uncacheable misses")
        .flags(total)
        ;
    overallMshrUncacheable =
        SUM_DEMAND(mshr_uncacheable) + SUM_NON_DEMAND(mshr_uncacheable);

    // MSHR miss latency statistics
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshr_uncacheable_lat[access_idx]
            .init(maxThreadsPerCPU)
            .name(name() + "." + cstr + "_mshr_uncacheable_latency")
            .desc("number of " + cstr + " MSHR uncacheable cycles")
            .flags(total | nozero | nonan)
            ;
    }

    overallMshrUncacheableLatency
        .name(name() + ".overall_mshr_uncacheable_latency")
        .desc("number of overall MSHR uncacheable cycles")
        .flags(total)
        ;
    overallMshrUncacheableLatency =
        SUM_DEMAND(mshr_uncacheable_lat) +
        SUM_NON_DEMAND(mshr_uncacheable_lat);

#if 0
    // MSHR access formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshrAccesses[access_idx]
            .name(name() + "." + cstr + "_mshr_accesses")
            .desc("number of " + cstr + " mshr accesses(hits+misses)")
            .flags(total | nozero | nonan)
            ;
        mshrAccesses[access_idx] =
            mshr_hits[access_idx] + mshr_misses[access_idx]
            + mshr_uncacheable[access_idx];
    }

    demandMshrAccesses
        .name(name() + ".demand_mshr_accesses")
        .desc("number of demand (read+write) mshr accesses")
        .flags(total | nozero | nonan)
        ;
    demandMshrAccesses = demandMshrHits + demandMshrMisses;

    overallMshrAccesses
        .name(name() + ".overall_mshr_accesses")
        .desc("number of overall (read+write) mshr accesses")
        .flags(total | nozero | nonan)
        ;
    overallMshrAccesses = overallMshrHits + overallMshrMisses
        + overallMshrUncacheable;
#endif

    // MSHR miss rate formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        mshrMissRate[access_idx]
            .name(name() + "." + cstr + "_mshr_miss_rate")
            .desc("mshr miss rate for " + cstr + " accesses")
            .flags(total | nozero | nonan)
            ;

        mshrMissRate[access_idx] =
            mshr_misses[access_idx] / accesses[access_idx];
    }

    demandMshrMissRate
        .name(name() + ".demand_mshr_miss_rate")
        .desc("mshr miss rate for demand accesses")
        .flags(total)
        ;
    demandMshrMissRate = demandMshrMisses / demandAccesses;

    overallMshrMissRate
        .name(name() + ".overall_mshr_miss_rate")
        .desc("mshr miss rate for overall accesses")
        .flags(total)
        ;
    overallMshrMissRate = overallMshrMisses / overallAccesses;

    // mshrMiss latency formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        avgMshrMissLatency[access_idx]
            .name(name() + "." + cstr + "_avg_mshr_miss_latency")
            .desc("average " + cstr + " mshr miss latency")
            .flags(total | nozero | nonan)
            ;

        avgMshrMissLatency[access_idx] =
            mshr_miss_latency[access_idx] / mshr_misses[access_idx];
    }

    demandAvgMshrMissLatency
        .name(name() + ".demand_avg_mshr_miss_latency")
        .desc("average overall mshr miss latency")
        .flags(total)
        ;
    demandAvgMshrMissLatency = demandMshrMissLatency / demandMshrMisses;

    overallAvgMshrMissLatency
        .name(name() + ".overall_avg_mshr_miss_latency")
        .desc("average overall mshr miss latency")
        .flags(total)
        ;
    overallAvgMshrMissLatency = overallMshrMissLatency / overallMshrMisses;

    // mshrUncacheable latency formulas
    for (int access_idx = 0; access_idx < MemCmd::NUM_MEM_CMDS; ++access_idx) {
        MemCmd cmd(access_idx);
        const string &cstr = cmd.toString();

        avgMshrUncacheableLatency[access_idx]
            .name(name() + "." + cstr + "_avg_mshr_uncacheable_latency")
            .desc("average " + cstr + " mshr uncacheable latency")
            .flags(total | nozero | nonan)
            ;

        avgMshrUncacheableLatency[access_idx] =
            mshr_uncacheable_lat[access_idx] / mshr_uncacheable[access_idx];
    }

    overallAvgMshrUncacheableLatency
        .name(name() + ".overall_avg_mshr_uncacheable_latency")
        .desc("average overall mshr uncacheable latency")
        .flags(total)
        ;
    overallAvgMshrUncacheableLatency = overallMshrUncacheableLatency / overallMshrUncacheable;

    mshr_cap_events
        .init(maxThreadsPerCPU)
        .name(name() + ".mshr_cap_events")
        .desc("number of times MSHR cap was activated")
        .flags(total)
        ;

    //software prefetching stats
    soft_prefetch_mshr_full
        .init(maxThreadsPerCPU)
        .name(name() + ".soft_prefetch_mshr_full")
        .desc("number of mshr full events for SW prefetching instrutions")
        .flags(total)
        ;

    mshr_no_allocate_misses
        .name(name() +".no_allocate_misses")
        .desc("Number of misses that were no-allocate")
        ;

    prefetchHits
        .name(name() + ".prefetch_hits")
        .desc("number of prefetched blocks that were accessed")
        ;

    prefetchAccuracy
        .name(name() + ".prefetch_accuracy")
        .desc("ratio of good prefetches to total prefetches")
        ;
    prefetchAccuracy = prefetchHits / mshr_misses[MemCmd::HardPFReq];

}

unsigned int
BaseCache::drain(Event *de)
{
    int count = memSidePort->drain(de) + cpuSidePort->drain(de);

    // Set status
    if (count != 0) {
        drainEvent = de;

        changeState(SimObject::Draining);
        return count;
    }

    changeState(SimObject::Drained);
    return 0;
}
