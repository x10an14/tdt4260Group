/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
 * Authors: Kevin Lim
 *          Korey Sewell
 */

#include "arch/locked_mem.hh"
#include "config/the_isa.hh"
#include "config/use_checker.hh"
#include "cpu/o3/lsq.hh"
#include "cpu/o3/lsq_unit.hh"
#include "base/str.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

#if USE_CHECKER
#include "cpu/checker/cpu.hh"
#endif

template<class Impl>
LSQUnit<Impl>::WritebackEvent::WritebackEvent(DynInstPtr &_inst, PacketPtr _pkt,
                                              LSQUnit *lsq_ptr)
    : inst(_inst), pkt(_pkt), lsqPtr(lsq_ptr)
{
    this->setFlags(Event::AutoDelete);
}

template<class Impl>
void
LSQUnit<Impl>::WritebackEvent::process()
{
    if (!lsqPtr->isSwitchedOut()) {
        lsqPtr->writeback(inst, pkt);
    }

    if (pkt->senderState)
        delete pkt->senderState;

    delete pkt->req;
    delete pkt;
}

template<class Impl>
const char *
LSQUnit<Impl>::WritebackEvent::description() const
{
    return "Store writeback";
}

template<class Impl>
void
LSQUnit<Impl>::completeDataAccess(PacketPtr pkt)
{
    LSQSenderState *state = dynamic_cast<LSQSenderState *>(pkt->senderState);
    DynInstPtr inst = state->inst;
    DPRINTF(IEW, "Writeback event [sn:%lli]\n", inst->seqNum);
    DPRINTF(Activity, "Activity: Writeback event [sn:%lli]\n", inst->seqNum);

    //iewStage->ldstQueue.removeMSHR(inst->threadNumber,inst->seqNum);

    assert(!pkt->wasNacked());

    // If this is a split access, wait until all packets are received.
    if (TheISA::HasUnalignedMemAcc && !state->complete()) {
        delete pkt->req;
        delete pkt;
        return;
    }

    if (isSwitchedOut() || inst->isSquashed()) {
        iewStage->decrWb(inst->seqNum);
    } else {
        if (!state->noWB) {
            if (!TheISA::HasUnalignedMemAcc || !state->isSplit ||
                !state->isLoad) {
                writeback(inst, pkt);
            } else {
                writeback(inst, state->mainPkt);
            }
        }

        if (inst->isStore()) {
            completeStore(state->idx);
        }
    }

    if (TheISA::HasUnalignedMemAcc && state->isSplit && state->isLoad) {
        delete state->mainPkt->req;
        delete state->mainPkt;
    }
    delete state;
    delete pkt->req;
    delete pkt;
}

template <class Impl>
LSQUnit<Impl>::LSQUnit()
    : loads(0), stores(0), storesToWB(0), stalled(false),
      isStoreBlocked(false), isLoadBlocked(false),
      loadBlockedHandled(false), hasPendingPkt(false)
{
}

template<class Impl>
void
LSQUnit<Impl>::init(O3CPU *cpu_ptr, IEW *iew_ptr, DerivO3CPUParams *params,
        LSQ *lsq_ptr, unsigned maxLQEntries, unsigned maxSQEntries,
        unsigned id)
{
    cpu = cpu_ptr;
    iewStage = iew_ptr;

    DPRINTF(LSQUnit, "Creating LSQUnit%i object.\n",id);

    switchedOut = false;

    lsq = lsq_ptr;

    lsqID = id;

    // Add 1 for the sentinel entry (they are circular queues).
    LQEntries = maxLQEntries + 1;
    SQEntries = maxSQEntries + 1;

    loadQueue.resize(LQEntries);
    storeQueue.resize(SQEntries);

    loadHead = loadTail = 0;

    storeHead = storeWBIdx = storeTail = 0;

    usedPorts = 0;
    cachePorts = params->cachePorts;

    retryPkt = NULL;
    memDepViolator = NULL;

    blockedLoadSeqNum = 0;
}

template<class Impl>
std::string
LSQUnit<Impl>::name() const
{
    if (Impl::MaxThreads == 1) {
        return iewStage->name() + ".lsq";
    } else {
        return iewStage->name() + ".lsq.thread." + to_string(lsqID);
    }
}

template<class Impl>
void
LSQUnit<Impl>::regStats()
{
    lsqForwLoads
        .name(name() + ".forwLoads")
        .desc("Number of loads that had data forwarded from stores");

    invAddrLoads
        .name(name() + ".invAddrLoads")
        .desc("Number of loads ignored due to an invalid address");

    lsqSquashedLoads
        .name(name() + ".squashedLoads")
        .desc("Number of loads squashed");

    lsqIgnoredResponses
        .name(name() + ".ignoredResponses")
        .desc("Number of memory responses ignored because the instruction is squashed");

    lsqMemOrderViolation
        .name(name() + ".memOrderViolation")
        .desc("Number of memory ordering violations");

    lsqSquashedStores
        .name(name() + ".squashedStores")
        .desc("Number of stores squashed");

    invAddrSwpfs
        .name(name() + ".invAddrSwpfs")
        .desc("Number of software prefetches ignored due to an invalid address");

    lsqBlockedLoads
        .name(name() + ".blockedLoads")
        .desc("Number of blocked loads due to partial load-store forwarding");

    lsqRescheduledLoads
        .name(name() + ".rescheduledLoads")
        .desc("Number of loads that were rescheduled");

    lsqCacheBlocked
        .name(name() + ".cacheBlocked")
        .desc("Number of times an access to memory failed due to the cache being blocked");
}

template<class Impl>
void
LSQUnit<Impl>::setDcachePort(Port *dcache_port)
{
    dcachePort = dcache_port;

#if USE_CHECKER
    if (cpu->checker) {
        cpu->checker->setDcachePort(dcachePort);
    }
#endif
}

template<class Impl>
void
LSQUnit<Impl>::clearLQ()
{
    loadQueue.clear();
}

template<class Impl>
void
LSQUnit<Impl>::clearSQ()
{
    storeQueue.clear();
}

template<class Impl>
void
LSQUnit<Impl>::switchOut()
{
    switchedOut = true;
    for (int i = 0; i < loadQueue.size(); ++i) {
        assert(!loadQueue[i]);
        loadQueue[i] = NULL;
    }

    assert(storesToWB == 0);
}

template<class Impl>
void
LSQUnit<Impl>::takeOverFrom()
{
    switchedOut = false;
    loads = stores = storesToWB = 0;

    loadHead = loadTail = 0;

    storeHead = storeWBIdx = storeTail = 0;

    usedPorts = 0;

    memDepViolator = NULL;

    blockedLoadSeqNum = 0;

    stalled = false;
    isLoadBlocked = false;
    loadBlockedHandled = false;
}

template<class Impl>
void
LSQUnit<Impl>::resizeLQ(unsigned size)
{
    unsigned size_plus_sentinel = size + 1;
    assert(size_plus_sentinel >= LQEntries);

    if (size_plus_sentinel > LQEntries) {
        while (size_plus_sentinel > loadQueue.size()) {
            DynInstPtr dummy;
            loadQueue.push_back(dummy);
            LQEntries++;
        }
    } else {
        LQEntries = size_plus_sentinel;
    }

}

template<class Impl>
void
LSQUnit<Impl>::resizeSQ(unsigned size)
{
    unsigned size_plus_sentinel = size + 1;
    if (size_plus_sentinel > SQEntries) {
        while (size_plus_sentinel > storeQueue.size()) {
            SQEntry dummy;
            storeQueue.push_back(dummy);
            SQEntries++;
        }
    } else {
        SQEntries = size_plus_sentinel;
    }
}

template <class Impl>
void
LSQUnit<Impl>::insert(DynInstPtr &inst)
{
    assert(inst->isMemRef());

    assert(inst->isLoad() || inst->isStore());

    if (inst->isLoad()) {
        insertLoad(inst);
    } else {
        insertStore(inst);
    }

    inst->setInLSQ();
}

template <class Impl>
void
LSQUnit<Impl>::insertLoad(DynInstPtr &load_inst)
{
    assert((loadTail + 1) % LQEntries != loadHead);
    assert(loads < LQEntries);

    DPRINTF(LSQUnit, "Inserting load PC %#x, idx:%i [sn:%lli]\n",
            load_inst->readPC(), loadTail, load_inst->seqNum);

    load_inst->lqIdx = loadTail;

    if (stores == 0) {
        load_inst->sqIdx = -1;
    } else {
        load_inst->sqIdx = storeTail;
    }

    loadQueue[loadTail] = load_inst;

    incrLdIdx(loadTail);

    ++loads;
}

template <class Impl>
void
LSQUnit<Impl>::insertStore(DynInstPtr &store_inst)
{
    // Make sure it is not full before inserting an instruction.
    assert((storeTail + 1) % SQEntries != storeHead);
    assert(stores < SQEntries);

    DPRINTF(LSQUnit, "Inserting store PC %#x, idx:%i [sn:%lli]\n",
            store_inst->readPC(), storeTail, store_inst->seqNum);

    store_inst->sqIdx = storeTail;
    store_inst->lqIdx = loadTail;

    storeQueue[storeTail] = SQEntry(store_inst);

    incrStIdx(storeTail);

    ++stores;
}

template <class Impl>
typename Impl::DynInstPtr
LSQUnit<Impl>::getMemDepViolator()
{
    DynInstPtr temp = memDepViolator;

    memDepViolator = NULL;

    return temp;
}

template <class Impl>
unsigned
LSQUnit<Impl>::numFreeEntries()
{
    unsigned free_lq_entries = LQEntries - loads;
    unsigned free_sq_entries = SQEntries - stores;

    // Both the LQ and SQ entries have an extra dummy entry to differentiate
    // empty/full conditions.  Subtract 1 from the free entries.
    if (free_lq_entries < free_sq_entries) {
        return free_lq_entries - 1;
    } else {
        return free_sq_entries - 1;
    }
}

template <class Impl>
int
LSQUnit<Impl>::numLoadsReady()
{
    int load_idx = loadHead;
    int retval = 0;

    while (load_idx != loadTail) {
        assert(loadQueue[load_idx]);

        if (loadQueue[load_idx]->readyToIssue()) {
            ++retval;
        }
    }

    return retval;
}

template <class Impl>
Fault
LSQUnit<Impl>::executeLoad(DynInstPtr &inst)
{
    using namespace TheISA;
    // Execute a specific load.
    Fault load_fault = NoFault;

    DPRINTF(LSQUnit, "Executing load PC %#x, [sn:%lli]\n",
            inst->readPC(),inst->seqNum);

    assert(!inst->isSquashed());

    load_fault = inst->initiateAcc();

    // If the instruction faulted, then we need to send it along to commit
    // without the instruction completing.
    if (load_fault != NoFault) {
        // Send this instruction to commit, also make sure iew stage
        // realizes there is activity.
        // Mark it as executed unless it is an uncached load that
        // needs to hit the head of commit.
        if (!(inst->hasRequest() && inst->uncacheable()) ||
            inst->isAtCommit()) {
            inst->setExecuted();
        }
        iewStage->instToCommit(inst);
        iewStage->activityThisCycle();
    } else if (!loadBlocked()) {
        assert(inst->effAddrValid);
        int load_idx = inst->lqIdx;
        incrLdIdx(load_idx);
        while (load_idx != loadTail) {
            // Really only need to check loads that have actually executed

            // @todo: For now this is extra conservative, detecting a
            // violation if the addresses match assuming all accesses
            // are quad word accesses.

            // @todo: Fix this, magic number being used here
            if (loadQueue[load_idx]->effAddrValid &&
                (loadQueue[load_idx]->effAddr >> 8) ==
                (inst->effAddr >> 8)) {
                // A load incorrectly passed this load.  Squash and refetch.
                // For now return a fault to show that it was unsuccessful.
                DynInstPtr violator = loadQueue[load_idx];
                if (!memDepViolator ||
                    (violator->seqNum < memDepViolator->seqNum)) {
                    memDepViolator = violator;
                } else {
                    break;
                }

                ++lsqMemOrderViolation;

                return genMachineCheckFault();
            }

            incrLdIdx(load_idx);
        }
    }

    return load_fault;
}

template <class Impl>
Fault
LSQUnit<Impl>::executeStore(DynInstPtr &store_inst)
{
    using namespace TheISA;
    // Make sure that a store exists.
    assert(stores != 0);

    int store_idx = store_inst->sqIdx;

    DPRINTF(LSQUnit, "Executing store PC %#x [sn:%lli]\n",
            store_inst->readPC(), store_inst->seqNum);

    assert(!store_inst->isSquashed());

    // Check the recently completed loads to see if any match this store's
    // address.  If so, then we have a memory ordering violation.
    int load_idx = store_inst->lqIdx;

    Fault store_fault = store_inst->initiateAcc();

    if (storeQueue[store_idx].size == 0) {
        DPRINTF(LSQUnit,"Fault on Store PC %#x, [sn:%lli],Size = 0\n",
                store_inst->readPC(),store_inst->seqNum);

        return store_fault;
    }

    assert(store_fault == NoFault);

    if (store_inst->isStoreConditional()) {
        // Store conditionals need to set themselves as able to
        // writeback if we haven't had a fault by here.
        storeQueue[store_idx].canWB = true;

        ++storesToWB;
    }

    assert(store_inst->effAddrValid);
    while (load_idx != loadTail) {
        // Really only need to check loads that have actually executed
        // It's safe to check all loads because effAddr is set to
        // InvalAddr when the dyn inst is created.

        // @todo: For now this is extra conservative, detecting a
        // violation if the addresses match assuming all accesses
        // are quad word accesses.

        // @todo: Fix this, magic number being used here
        if (loadQueue[load_idx]->effAddrValid &&
            (loadQueue[load_idx]->effAddr >> 8) ==
            (store_inst->effAddr >> 8)) {
            // A load incorrectly passed this store.  Squash and refetch.
            // For now return a fault to show that it was unsuccessful.
            DynInstPtr violator = loadQueue[load_idx];
            if (!memDepViolator ||
                (violator->seqNum < memDepViolator->seqNum)) {
                memDepViolator = violator;
            } else {
                break;
            }

            ++lsqMemOrderViolation;

            return genMachineCheckFault();
        }

        incrLdIdx(load_idx);
    }

    return store_fault;
}

template <class Impl>
void
LSQUnit<Impl>::commitLoad()
{
    assert(loadQueue[loadHead]);

    DPRINTF(LSQUnit, "Committing head load instruction, PC %#x\n",
            loadQueue[loadHead]->readPC());

    loadQueue[loadHead] = NULL;

    incrLdIdx(loadHead);

    --loads;
}

template <class Impl>
void
LSQUnit<Impl>::commitLoads(InstSeqNum &youngest_inst)
{
    assert(loads == 0 || loadQueue[loadHead]);

    while (loads != 0 && loadQueue[loadHead]->seqNum <= youngest_inst) {
        commitLoad();
    }
}

template <class Impl>
void
LSQUnit<Impl>::commitStores(InstSeqNum &youngest_inst)
{
    assert(stores == 0 || storeQueue[storeHead].inst);

    int store_idx = storeHead;

    while (store_idx != storeTail) {
        assert(storeQueue[store_idx].inst);
        // Mark any stores that are now committed and have not yet
        // been marked as able to write back.
        if (!storeQueue[store_idx].canWB) {
            if (storeQueue[store_idx].inst->seqNum > youngest_inst) {
                break;
            }
            DPRINTF(LSQUnit, "Marking store as able to write back, PC "
                    "%#x [sn:%lli]\n",
                    storeQueue[store_idx].inst->readPC(),
                    storeQueue[store_idx].inst->seqNum);

            storeQueue[store_idx].canWB = true;

            ++storesToWB;
        }

        incrStIdx(store_idx);
    }
}

template <class Impl>
void
LSQUnit<Impl>::writebackPendingStore()
{
    if (hasPendingPkt) {
        assert(pendingPkt != NULL);

        // If the cache is blocked, this will store the packet for retry.
        if (sendStore(pendingPkt)) {
            storePostSend(pendingPkt);
        }
        pendingPkt = NULL;
        hasPendingPkt = false;
    }
}

template <class Impl>
void
LSQUnit<Impl>::writebackStores()
{
    // First writeback the second packet from any split store that didn't
    // complete last cycle because there weren't enough cache ports available.
    if (TheISA::HasUnalignedMemAcc) {
        writebackPendingStore();
    }

    while (storesToWB > 0 &&
           storeWBIdx != storeTail &&
           storeQueue[storeWBIdx].inst &&
           storeQueue[storeWBIdx].canWB &&
           usedPorts < cachePorts) {

        if (isStoreBlocked || lsq->cacheBlocked()) {
            DPRINTF(LSQUnit, "Unable to write back any more stores, cache"
                    " is blocked!\n");
            break;
        }

        // Store didn't write any data so no need to write it back to
        // memory.
        if (storeQueue[storeWBIdx].size == 0) {
            completeStore(storeWBIdx);

            incrStIdx(storeWBIdx);

            continue;
        }

        ++usedPorts;

        if (storeQueue[storeWBIdx].inst->isDataPrefetch()) {
            incrStIdx(storeWBIdx);

            continue;
        }

        assert(storeQueue[storeWBIdx].req);
        assert(!storeQueue[storeWBIdx].committed);

        if (TheISA::HasUnalignedMemAcc && storeQueue[storeWBIdx].isSplit) {
            assert(storeQueue[storeWBIdx].sreqLow);
            assert(storeQueue[storeWBIdx].sreqHigh);
        }

        DynInstPtr inst = storeQueue[storeWBIdx].inst;

        Request *req = storeQueue[storeWBIdx].req;
        storeQueue[storeWBIdx].committed = true;

        assert(!inst->memData);
        inst->memData = new uint8_t[64];

        memcpy(inst->memData, storeQueue[storeWBIdx].data, req->getSize());

        MemCmd command =
            req->isSwap() ? MemCmd::SwapReq :
            (req->isLLSC() ? MemCmd::StoreCondReq : MemCmd::WriteReq);
        PacketPtr data_pkt;
        PacketPtr snd_data_pkt = NULL;

        LSQSenderState *state = new LSQSenderState;
        state->isLoad = false;
        state->idx = storeWBIdx;
        state->inst = inst;

        if (!TheISA::HasUnalignedMemAcc || !storeQueue[storeWBIdx].isSplit) {

            // Build a single data packet if the store isn't split.
            data_pkt = new Packet(req, command, Packet::Broadcast);
            data_pkt->dataStatic(inst->memData);
            data_pkt->senderState = state;
        } else {
            RequestPtr sreqLow = storeQueue[storeWBIdx].sreqLow;
            RequestPtr sreqHigh = storeQueue[storeWBIdx].sreqHigh;

            // Create two packets if the store is split in two.
            data_pkt = new Packet(sreqLow, command, Packet::Broadcast);
            snd_data_pkt = new Packet(sreqHigh, command, Packet::Broadcast);

            data_pkt->dataStatic(inst->memData);
            snd_data_pkt->dataStatic(inst->memData + sreqLow->getSize());

            data_pkt->senderState = state;
            snd_data_pkt->senderState = state;

            state->isSplit = true;
            state->outstanding = 2;

            // Can delete the main request now.
            delete req;
            req = sreqLow;
        }

        DPRINTF(LSQUnit, "D-Cache: Writing back store idx:%i PC:%#x "
                "to Addr:%#x, data:%#x [sn:%lli]\n",
                storeWBIdx, inst->readPC(),
                req->getPaddr(), (int)*(inst->memData),
                inst->seqNum);

        // @todo: Remove this SC hack once the memory system handles it.
        if (inst->isStoreConditional()) {
            assert(!storeQueue[storeWBIdx].isSplit);
            // Disable recording the result temporarily.  Writing to
            // misc regs normally updates the result, but this is not
            // the desired behavior when handling store conditionals.
            inst->recordResult = false;
            bool success = TheISA::handleLockedWrite(inst.get(), req);
            inst->recordResult = true;

            if (!success) {
                // Instantly complete this store.
                DPRINTF(LSQUnit, "Store conditional [sn:%lli] failed.  "
                        "Instantly completing it.\n",
                        inst->seqNum);
                WritebackEvent *wb = new WritebackEvent(inst, data_pkt, this);
                cpu->schedule(wb, curTick + 1);
                completeStore(storeWBIdx);
                incrStIdx(storeWBIdx);
                continue;
            }
        } else {
            // Non-store conditionals do not need a writeback.
            state->noWB = true;
        }

        if (!sendStore(data_pkt)) {
            DPRINTF(IEW, "D-Cache became blocked when writing [sn:%lli], will"
                    "retry later\n",
                    inst->seqNum);

            // Need to store the second packet, if split.
            if (TheISA::HasUnalignedMemAcc && storeQueue[storeWBIdx].isSplit) {
                state->pktToSend = true;
                state->pendingPacket = snd_data_pkt;
            }
        } else {

            // If split, try to send the second packet too
            if (TheISA::HasUnalignedMemAcc && storeQueue[storeWBIdx].isSplit) {
                assert(snd_data_pkt);

                // Ensure there are enough ports to use.
                if (usedPorts < cachePorts) {
                    ++usedPorts;
                    if (sendStore(snd_data_pkt)) {
                        storePostSend(snd_data_pkt);
                    } else {
                        DPRINTF(IEW, "D-Cache became blocked when writing"
                                " [sn:%lli] second packet, will retry later\n",
                                inst->seqNum);
                    }
                } else {

                    // Store the packet for when there's free ports.
                    assert(pendingPkt == NULL);
                    pendingPkt = snd_data_pkt;
                    hasPendingPkt = true;
                }
            } else {

                // Not a split store.
                storePostSend(data_pkt);
            }
        }
    }

    // Not sure this should set it to 0.
    usedPorts = 0;

    assert(stores >= 0 && storesToWB >= 0);
}

/*template <class Impl>
void
LSQUnit<Impl>::removeMSHR(InstSeqNum seqNum)
{
    list<InstSeqNum>::iterator mshr_it = find(mshrSeqNums.begin(),
                                              mshrSeqNums.end(),
                                              seqNum);

    if (mshr_it != mshrSeqNums.end()) {
        mshrSeqNums.erase(mshr_it);
        DPRINTF(LSQUnit, "Removing MSHR. count = %i\n",mshrSeqNums.size());
    }
}*/

template <class Impl>
void
LSQUnit<Impl>::squash(const InstSeqNum &squashed_num)
{
    DPRINTF(LSQUnit, "Squashing until [sn:%lli]!"
            "(Loads:%i Stores:%i)\n", squashed_num, loads, stores);

    int load_idx = loadTail;
    decrLdIdx(load_idx);

    while (loads != 0 && loadQueue[load_idx]->seqNum > squashed_num) {
        DPRINTF(LSQUnit,"Load Instruction PC %#x squashed, "
                "[sn:%lli]\n",
                loadQueue[load_idx]->readPC(),
                loadQueue[load_idx]->seqNum);

        if (isStalled() && load_idx == stallingLoadIdx) {
            stalled = false;
            stallingStoreIsn = 0;
            stallingLoadIdx = 0;
        }

        // Clear the smart pointer to make sure it is decremented.
        loadQueue[load_idx]->setSquashed();
        loadQueue[load_idx] = NULL;
        --loads;

        // Inefficient!
        loadTail = load_idx;

        decrLdIdx(load_idx);
        ++lsqSquashedLoads;
    }

    if (isLoadBlocked) {
        if (squashed_num < blockedLoadSeqNum) {
            isLoadBlocked = false;
            loadBlockedHandled = false;
            blockedLoadSeqNum = 0;
        }
    }

    if (memDepViolator && squashed_num < memDepViolator->seqNum) {
        memDepViolator = NULL;
    }

    int store_idx = storeTail;
    decrStIdx(store_idx);

    while (stores != 0 &&
           storeQueue[store_idx].inst->seqNum > squashed_num) {
        // Instructions marked as can WB are already committed.
        if (storeQueue[store_idx].canWB) {
            break;
        }

        DPRINTF(LSQUnit,"Store Instruction PC %#x squashed, "
                "idx:%i [sn:%lli]\n",
                storeQueue[store_idx].inst->readPC(),
                store_idx, storeQueue[store_idx].inst->seqNum);

        // I don't think this can happen.  It should have been cleared
        // by the stalling load.
        if (isStalled() &&
            storeQueue[store_idx].inst->seqNum == stallingStoreIsn) {
            panic("Is stalled should have been cleared by stalling load!\n");
            stalled = false;
            stallingStoreIsn = 0;
        }

        // Clear the smart pointer to make sure it is decremented.
        storeQueue[store_idx].inst->setSquashed();
        storeQueue[store_idx].inst = NULL;
        storeQueue[store_idx].canWB = 0;

        // Must delete request now that it wasn't handed off to
        // memory.  This is quite ugly.  @todo: Figure out the proper
        // place to really handle request deletes.
        delete storeQueue[store_idx].req;
        if (TheISA::HasUnalignedMemAcc && storeQueue[store_idx].isSplit) {
            delete storeQueue[store_idx].sreqLow;
            delete storeQueue[store_idx].sreqHigh;

            storeQueue[store_idx].sreqLow = NULL;
            storeQueue[store_idx].sreqHigh = NULL;
        }

        storeQueue[store_idx].req = NULL;
        --stores;

        // Inefficient!
        storeTail = store_idx;

        decrStIdx(store_idx);
        ++lsqSquashedStores;
    }
}

template <class Impl>
void
LSQUnit<Impl>::storePostSend(PacketPtr pkt)
{
    if (isStalled() &&
        storeQueue[storeWBIdx].inst->seqNum == stallingStoreIsn) {
        DPRINTF(LSQUnit, "Unstalling, stalling store [sn:%lli] "
                "load idx:%i\n",
                stallingStoreIsn, stallingLoadIdx);
        stalled = false;
        stallingStoreIsn = 0;
        iewStage->replayMemInst(loadQueue[stallingLoadIdx]);
    }

    if (!storeQueue[storeWBIdx].inst->isStoreConditional()) {
        // The store is basically completed at this time. This
        // only works so long as the checker doesn't try to
        // verify the value in memory for stores.
        storeQueue[storeWBIdx].inst->setCompleted();
#if USE_CHECKER
        if (cpu->checker) {
            cpu->checker->verify(storeQueue[storeWBIdx].inst);
        }
#endif
    }

    incrStIdx(storeWBIdx);
}

template <class Impl>
void
LSQUnit<Impl>::writeback(DynInstPtr &inst, PacketPtr pkt)
{
    iewStage->wakeCPU();

    // Squashed instructions do not need to complete their access.
    if (inst->isSquashed()) {
        iewStage->decrWb(inst->seqNum);
        assert(!inst->isStore());
        ++lsqIgnoredResponses;
        return;
    }

    if (!inst->isExecuted()) {
        inst->setExecuted();

        // Complete access to copy data to proper place.
        inst->completeAcc(pkt);
    }

    // Need to insert instruction into queue to commit
    iewStage->instToCommit(inst);

    iewStage->activityThisCycle();
}

template <class Impl>
void
LSQUnit<Impl>::completeStore(int store_idx)
{
    assert(storeQueue[store_idx].inst);
    storeQueue[store_idx].completed = true;
    --storesToWB;
    // A bit conservative because a store completion may not free up entries,
    // but hopefully avoids two store completions in one cycle from making
    // the CPU tick twice.
    cpu->wakeCPU();
    cpu->activityThisCycle();

    if (store_idx == storeHead) {
        do {
            incrStIdx(storeHead);

            --stores;
        } while (storeQueue[storeHead].completed &&
                 storeHead != storeTail);

        iewStage->updateLSQNextCycle = true;
    }

    DPRINTF(LSQUnit, "Completing store [sn:%lli], idx:%i, store head "
            "idx:%i\n",
            storeQueue[store_idx].inst->seqNum, store_idx, storeHead);

    if (isStalled() &&
        storeQueue[store_idx].inst->seqNum == stallingStoreIsn) {
        DPRINTF(LSQUnit, "Unstalling, stalling store [sn:%lli] "
                "load idx:%i\n",
                stallingStoreIsn, stallingLoadIdx);
        stalled = false;
        stallingStoreIsn = 0;
        iewStage->replayMemInst(loadQueue[stallingLoadIdx]);
    }

    storeQueue[store_idx].inst->setCompleted();

    // Tell the checker we've completed this instruction.  Some stores
    // may get reported twice to the checker, but the checker can
    // handle that case.
#if USE_CHECKER
    if (cpu->checker) {
        cpu->checker->verify(storeQueue[store_idx].inst);
    }
#endif
}

template <class Impl>
bool
LSQUnit<Impl>::sendStore(PacketPtr data_pkt)
{
    if (!dcachePort->sendTiming(data_pkt)) {
        // Need to handle becoming blocked on a store.
        isStoreBlocked = true;
        ++lsqCacheBlocked;
        assert(retryPkt == NULL);
        retryPkt = data_pkt;
        lsq->setRetryTid(lsqID);
        return false;
    }
    return true;
}

template <class Impl>
void
LSQUnit<Impl>::recvRetry()
{
    if (isStoreBlocked) {
        DPRINTF(LSQUnit, "Receiving retry: store blocked\n");
        assert(retryPkt != NULL);

        if (dcachePort->sendTiming(retryPkt)) {
            LSQSenderState *state =
                dynamic_cast<LSQSenderState *>(retryPkt->senderState);

            // Don't finish the store unless this is the last packet.
            if (!TheISA::HasUnalignedMemAcc || !state->pktToSend) {
                storePostSend(retryPkt);
            }
            retryPkt = NULL;
            isStoreBlocked = false;
            lsq->setRetryTid(InvalidThreadID);

            // Send any outstanding packet.
            if (TheISA::HasUnalignedMemAcc && state->pktToSend) {
                assert(state->pendingPacket);
                if (sendStore(state->pendingPacket)) {
                    storePostSend(state->pendingPacket);
                }
            }
        } else {
            // Still blocked!
            ++lsqCacheBlocked;
            lsq->setRetryTid(lsqID);
        }
    } else if (isLoadBlocked) {
        DPRINTF(LSQUnit, "Loads squash themselves and all younger insts, "
                "no need to resend packet.\n");
    } else {
        DPRINTF(LSQUnit, "Retry received but LSQ is no longer blocked.\n");
    }
}

template <class Impl>
inline void
LSQUnit<Impl>::incrStIdx(int &store_idx)
{
    if (++store_idx >= SQEntries)
        store_idx = 0;
}

template <class Impl>
inline void
LSQUnit<Impl>::decrStIdx(int &store_idx)
{
    if (--store_idx < 0)
        store_idx += SQEntries;
}

template <class Impl>
inline void
LSQUnit<Impl>::incrLdIdx(int &load_idx)
{
    if (++load_idx >= LQEntries)
        load_idx = 0;
}

template <class Impl>
inline void
LSQUnit<Impl>::decrLdIdx(int &load_idx)
{
    if (--load_idx < 0)
        load_idx += LQEntries;
}

template <class Impl>
void
LSQUnit<Impl>::dumpInsts()
{
    cprintf("Load store queue: Dumping instructions.\n");
    cprintf("Load queue size: %i\n", loads);
    cprintf("Load queue: ");

    int load_idx = loadHead;

    while (load_idx != loadTail && loadQueue[load_idx]) {
        cprintf("%#x ", loadQueue[load_idx]->readPC());

        incrLdIdx(load_idx);
    }

    cprintf("Store queue size: %i\n", stores);
    cprintf("Store queue: ");

    int store_idx = storeHead;

    while (store_idx != storeTail && storeQueue[store_idx].inst) {
        cprintf("%#x ", storeQueue[store_idx].inst->readPC());

        incrStIdx(store_idx);
    }

    cprintf("\n");
}
