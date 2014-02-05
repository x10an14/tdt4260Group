/*
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
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
 */

#include <iostream>
#include <set>
#include <string>
#include <sstream>

#include "base/cprintf.hh"
#include "base/trace.hh"
#include "config/the_isa.hh"
#include "cpu/base_dyn_inst.hh"
#include "cpu/exetrace.hh"
#include "mem/request.hh"
#include "sim/faults.hh"

#define NOHASH
#ifndef NOHASH

#include "base/hashmap.hh"

unsigned int MyHashFunc(const BaseDynInst *addr)
{
    unsigned a = (unsigned)addr;
    unsigned hash = (((a >> 14) ^ ((a >> 2) & 0xffff))) & 0x7FFFFFFF;

    return hash;
}

typedef m5::hash_map<const BaseDynInst *, const BaseDynInst *, MyHashFunc>
my_hash_t;

my_hash_t thishash;
#endif

template <class Impl>
BaseDynInst<Impl>::BaseDynInst(StaticInstPtr _staticInst,
                               Addr inst_PC, Addr inst_NPC,
                               Addr inst_MicroPC,
                               Addr pred_PC, Addr pred_NPC,
                               Addr pred_MicroPC,
                               InstSeqNum seq_num, ImplCPU *cpu)
  : staticInst(_staticInst), traceData(NULL), cpu(cpu)
{
    seqNum = seq_num;

    bool nextIsMicro =
        staticInst->isMicroop() && !staticInst->isLastMicroop();

    PC = inst_PC;
    microPC = inst_MicroPC;
    if (nextIsMicro) {
        nextPC = inst_PC;
        nextNPC = inst_NPC;
        nextMicroPC = microPC + 1;
    } else {
        nextPC = inst_NPC;
        nextNPC = nextPC + sizeof(TheISA::MachInst);
        nextMicroPC = 0;
    }
    predPC = pred_PC;
    predNPC = pred_NPC;
    predMicroPC = pred_MicroPC;
    predTaken = false;

    initVars();
}

template <class Impl>
BaseDynInst<Impl>::BaseDynInst(TheISA::ExtMachInst inst,
                               Addr inst_PC, Addr inst_NPC,
                               Addr inst_MicroPC,
                               Addr pred_PC, Addr pred_NPC,
                               Addr pred_MicroPC,
                               InstSeqNum seq_num, ImplCPU *cpu)
  : staticInst(inst, inst_PC), traceData(NULL), cpu(cpu)
{
    seqNum = seq_num;

    bool nextIsMicro =
        staticInst->isMicroop() && !staticInst->isLastMicroop();

    PC = inst_PC;
    microPC = inst_MicroPC;
    if (nextIsMicro) {
        nextPC = inst_PC;
        nextNPC = inst_NPC;
        nextMicroPC = microPC + 1;
    } else {
        nextPC = inst_NPC;
        nextNPC = nextPC + sizeof(TheISA::MachInst);
        nextMicroPC = 0;
    }
    predPC = pred_PC;
    predNPC = pred_NPC;
    predMicroPC = pred_MicroPC;
    predTaken = false;

    initVars();
}

template <class Impl>
BaseDynInst<Impl>::BaseDynInst(StaticInstPtr &_staticInst)
    : staticInst(_staticInst), traceData(NULL)
{
    seqNum = 0;
    initVars();
}

template <class Impl>
void
BaseDynInst<Impl>::initVars()
{
    memData = NULL;
    effAddr = 0;
    effAddrValid = false;
    physEffAddr = 0;

    isUncacheable = false;
    reqMade = false;
    readyRegs = 0;

    instResult.integer = 0;
    recordResult = true;

    status.reset();

    eaCalcDone = false;
    memOpDone = false;

    lqIdx = -1;
    sqIdx = -1;

    // Eventually make this a parameter.
    threadNumber = 0;

    // Also make this a parameter, or perhaps get it from xc or cpu.
    asid = 0;

    // Initialize the fault to be NoFault.
    fault = NoFault;

#ifndef NDEBUG
    ++cpu->instcount;

    if (cpu->instcount > 1500) {
#ifdef DEBUG
        cpu->dumpInsts();
        dumpSNList();
#endif
        assert(cpu->instcount <= 1500);
    }

    DPRINTF(DynInst,
        "DynInst: [sn:%lli] Instruction created. Instcount for %s = %i\n",
        seqNum, cpu->name(), cpu->instcount);
#endif

#ifdef DEBUG
    cpu->snList.insert(seqNum);
#endif
}

template <class Impl>
BaseDynInst<Impl>::~BaseDynInst()
{
    if (memData) {
        delete [] memData;
    }

    if (traceData) {
        delete traceData;
    }

    fault = NoFault;

#ifndef NDEBUG
    --cpu->instcount;

    DPRINTF(DynInst,
        "DynInst: [sn:%lli] Instruction destroyed. Instcount for %s = %i\n",
        seqNum, cpu->name(), cpu->instcount);
#endif
#ifdef DEBUG
    cpu->snList.erase(seqNum);
#endif
}

#ifdef DEBUG
template <class Impl>
void
BaseDynInst<Impl>::dumpSNList()
{
    std::set<InstSeqNum>::iterator sn_it = cpu->snList.begin();

    int count = 0;
    while (sn_it != cpu->snList.end()) {
        cprintf("%i: [sn:%lli] not destroyed\n", count, (*sn_it));
        count++;
        sn_it++;
    }
}
#endif

template <class Impl>
void
BaseDynInst<Impl>::prefetch(Addr addr, unsigned flags)
{
    // This is the "functional" implementation of prefetch.  Not much
    // happens here since prefetches don't affect the architectural
    // state.
/*
    // Generate a MemReq so we can translate the effective address.
    MemReqPtr req = new MemReq(addr, thread->getXCProxy(), 1, flags);
    req->asid = asid;

    // Prefetches never cause faults.
    fault = NoFault;

    // note this is a local, not BaseDynInst::fault
    Fault trans_fault = cpu->translateDataReadReq(req);

    if (trans_fault == NoFault && !(req->isUncacheable())) {
        // It's a valid address to cacheable space.  Record key MemReq
        // parameters so we can generate another one just like it for
        // the timing access without calling translate() again (which
        // might mess up the TLB).
        effAddr = req->vaddr;
        physEffAddr = req->paddr;
        memReqFlags = req->flags;
    } else {
        // Bogus address (invalid or uncacheable space).  Mark it by
        // setting the eff_addr to InvalidAddr.
        effAddr = physEffAddr = MemReq::inval_addr;
    }

    if (traceData) {
        traceData->setAddr(addr);
    }
*/
}

template <class Impl>
void
BaseDynInst<Impl>::writeHint(Addr addr, int size, unsigned flags)
{
    // Not currently supported.
}

/**
 * @todo Need to find a way to get the cache block size here.
 */
template <class Impl>
Fault
BaseDynInst<Impl>::copySrcTranslate(Addr src)
{
    // Not currently supported.
    return NoFault;
}

/**
 * @todo Need to find a way to get the cache block size here.
 */
template <class Impl>
Fault
BaseDynInst<Impl>::copy(Addr dest)
{
    // Not currently supported.
    return NoFault;
}

template <class Impl>
void
BaseDynInst<Impl>::dump()
{
    cprintf("T%d : %#08d `", threadNumber, PC);
    std::cout << staticInst->disassemble(PC);
    cprintf("'\n");
}

template <class Impl>
void
BaseDynInst<Impl>::dump(std::string &outstring)
{
    std::ostringstream s;
    s << "T" << threadNumber << " : 0x" << PC << " "
      << staticInst->disassemble(PC);

    outstring = s.str();
}

template <class Impl>
void
BaseDynInst<Impl>::markSrcRegReady()
{
    if (++readyRegs == numSrcRegs()) {
        setCanIssue();
    }
}

template <class Impl>
void
BaseDynInst<Impl>::markSrcRegReady(RegIndex src_idx)
{
    _readySrcRegIdx[src_idx] = true;

    markSrcRegReady();
}

template <class Impl>
bool
BaseDynInst<Impl>::eaSrcsReady()
{
    // For now I am assuming that src registers 1..n-1 are the ones that the
    // EA calc depends on.  (i.e. src reg 0 is the source of the data to be
    // stored)

    for (int i = 1; i < numSrcRegs(); ++i) {
        if (!_readySrcRegIdx[i])
            return false;
    }

    return true;
}
