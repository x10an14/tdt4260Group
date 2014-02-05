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
 *          Korey Sewell
 */

#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "config/use_checker.hh"
#include "cpu/activity.hh"
#include "cpu/simple_thread.hh"
#include "cpu/thread_context.hh"
#include "cpu/o3/isa_specific.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/thread_context.hh"
#include "enums/MemoryMode.hh"
#include "sim/core.hh"
#include "sim/stat_control.hh"

#if FULL_SYSTEM
#include "cpu/quiesce_event.hh"
#include "sim/system.hh"
#else
#include "sim/process.hh"
#endif

#if USE_CHECKER
#include "cpu/checker/cpu.hh"
#endif

#if THE_ISA == ALPHA_ISA
#include "arch/alpha/osfpal.hh"
#endif

class BaseCPUParams;

using namespace TheISA;
using namespace std;

BaseO3CPU::BaseO3CPU(BaseCPUParams *params)
    : BaseCPU(params)
{
}

void
BaseO3CPU::regStats()
{
    BaseCPU::regStats();
}

template <class Impl>
FullO3CPU<Impl>::TickEvent::TickEvent(FullO3CPU<Impl> *c)
    : Event(CPU_Tick_Pri), cpu(c)
{
}

template <class Impl>
void
FullO3CPU<Impl>::TickEvent::process()
{
    cpu->tick();
}

template <class Impl>
const char *
FullO3CPU<Impl>::TickEvent::description() const
{
    return "FullO3CPU tick";
}

template <class Impl>
FullO3CPU<Impl>::ActivateThreadEvent::ActivateThreadEvent()
    : Event(CPU_Switch_Pri)
{
}

template <class Impl>
void
FullO3CPU<Impl>::ActivateThreadEvent::init(int thread_num,
                                           FullO3CPU<Impl> *thread_cpu)
{
    tid = thread_num;
    cpu = thread_cpu;
}

template <class Impl>
void
FullO3CPU<Impl>::ActivateThreadEvent::process()
{
    cpu->activateThread(tid);
}

template <class Impl>
const char *
FullO3CPU<Impl>::ActivateThreadEvent::description() const
{
    return "FullO3CPU \"Activate Thread\"";
}

template <class Impl>
FullO3CPU<Impl>::DeallocateContextEvent::DeallocateContextEvent()
    : Event(CPU_Tick_Pri), tid(0), remove(false), cpu(NULL)
{
}

template <class Impl>
void
FullO3CPU<Impl>::DeallocateContextEvent::init(int thread_num,
                                              FullO3CPU<Impl> *thread_cpu)
{
    tid = thread_num;
    cpu = thread_cpu;
    remove = false;
}

template <class Impl>
void
FullO3CPU<Impl>::DeallocateContextEvent::process()
{
    cpu->deactivateThread(tid);
    if (remove)
        cpu->removeThread(tid);
}

template <class Impl>
const char *
FullO3CPU<Impl>::DeallocateContextEvent::description() const
{
    return "FullO3CPU \"Deallocate Context\"";
}

template <class Impl>
FullO3CPU<Impl>::FullO3CPU(DerivO3CPUParams *params)
    : BaseO3CPU(params),
      itb(params->itb),
      dtb(params->dtb),
      tickEvent(this),
#ifndef NDEBUG
      instcount(0),
#endif
      removeInstsThisCycle(false),
      fetch(this, params),
      decode(this, params),
      rename(this, params),
      iew(this, params),
      commit(this, params),

      regFile(this, params->numPhysIntRegs,
              params->numPhysFloatRegs),

      freeList(params->numThreads,
               TheISA::NumIntRegs, params->numPhysIntRegs,
               TheISA::NumFloatRegs, params->numPhysFloatRegs),

      rob(this,
          params->numROBEntries, params->squashWidth,
          params->smtROBPolicy, params->smtROBThreshold,
          params->numThreads),

      scoreboard(params->numThreads,
                 TheISA::NumIntRegs, params->numPhysIntRegs,
                 TheISA::NumFloatRegs, params->numPhysFloatRegs,
                 TheISA::NumMiscRegs * numThreads,
                 TheISA::ZeroReg),

      timeBuffer(params->backComSize, params->forwardComSize),
      fetchQueue(params->backComSize, params->forwardComSize),
      decodeQueue(params->backComSize, params->forwardComSize),
      renameQueue(params->backComSize, params->forwardComSize),
      iewQueue(params->backComSize, params->forwardComSize),
      activityRec(name(), NumStages,
                  params->backComSize + params->forwardComSize,
                  params->activity),

      globalSeqNum(1),
#if FULL_SYSTEM
      system(params->system),
#endif // FULL_SYSTEM
      drainCount(0),
      deferRegistration(params->defer_registration)
{
    if (!deferRegistration) {
        _status = Running;
    } else {
        _status = Idle;
    }

#if USE_CHECKER
    if (params->checker) {
        BaseCPU *temp_checker = params->checker;
        checker = dynamic_cast<Checker<DynInstPtr> *>(temp_checker);
#if FULL_SYSTEM
        checker->setSystem(params->system);
#endif
    } else {
        checker = NULL;
    }
#endif // USE_CHECKER

#if !FULL_SYSTEM
    thread.resize(numThreads);
    tids.resize(numThreads);
#endif

    // The stages also need their CPU pointer setup.  However this
    // must be done at the upper level CPU because they have pointers
    // to the upper level CPU, and not this FullO3CPU.

    // Set up Pointers to the activeThreads list for each stage
    fetch.setActiveThreads(&activeThreads);
    decode.setActiveThreads(&activeThreads);
    rename.setActiveThreads(&activeThreads);
    iew.setActiveThreads(&activeThreads);
    commit.setActiveThreads(&activeThreads);

    // Give each of the stages the time buffer they will use.
    fetch.setTimeBuffer(&timeBuffer);
    decode.setTimeBuffer(&timeBuffer);
    rename.setTimeBuffer(&timeBuffer);
    iew.setTimeBuffer(&timeBuffer);
    commit.setTimeBuffer(&timeBuffer);

    // Also setup each of the stages' queues.
    fetch.setFetchQueue(&fetchQueue);
    decode.setFetchQueue(&fetchQueue);
    commit.setFetchQueue(&fetchQueue);
    decode.setDecodeQueue(&decodeQueue);
    rename.setDecodeQueue(&decodeQueue);
    rename.setRenameQueue(&renameQueue);
    iew.setRenameQueue(&renameQueue);
    iew.setIEWQueue(&iewQueue);
    commit.setIEWQueue(&iewQueue);
    commit.setRenameQueue(&renameQueue);

    commit.setIEWStage(&iew);
    rename.setIEWStage(&iew);
    rename.setCommitStage(&commit);

#if !FULL_SYSTEM
    ThreadID active_threads = params->workload.size();

    if (active_threads > Impl::MaxThreads) {
        panic("Workload Size too large. Increase the 'MaxThreads'"
              "constant in your O3CPU impl. file (e.g. o3/alpha/impl.hh) or "
              "edit your workload size.");
    }
#else
    ThreadID active_threads = 1;
#endif

    //Make Sure That this a Valid Architeture
    assert(params->numPhysIntRegs   >= numThreads * TheISA::NumIntRegs);
    assert(params->numPhysFloatRegs >= numThreads * TheISA::NumFloatRegs);

    rename.setScoreboard(&scoreboard);
    iew.setScoreboard(&scoreboard);

    // Setup the rename map for whichever stages need it.
    PhysRegIndex lreg_idx = 0;
    PhysRegIndex freg_idx = params->numPhysIntRegs; //Index to 1 after int regs

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        bool bindRegs = (tid <= active_threads - 1);

        commitRenameMap[tid].init(TheISA::NumIntRegs,
                                  params->numPhysIntRegs,
                                  lreg_idx,            //Index for Logical. Regs

                                  TheISA::NumFloatRegs,
                                  params->numPhysFloatRegs,
                                  freg_idx,            //Index for Float Regs

                                  TheISA::NumMiscRegs,

                                  TheISA::ZeroReg,
                                  TheISA::ZeroReg,

                                  tid,
                                  false);

        renameMap[tid].init(TheISA::NumIntRegs,
                            params->numPhysIntRegs,
                            lreg_idx,                  //Index for Logical. Regs

                            TheISA::NumFloatRegs,
                            params->numPhysFloatRegs,
                            freg_idx,                  //Index for Float Regs

                            TheISA::NumMiscRegs,

                            TheISA::ZeroReg,
                            TheISA::ZeroReg,

                            tid,
                            bindRegs);

        activateThreadEvent[tid].init(tid, this);
        deallocateContextEvent[tid].init(tid, this);
    }

    rename.setRenameMap(renameMap);
    commit.setRenameMap(commitRenameMap);

    // Give renameMap & rename stage access to the freeList;
    for (ThreadID tid = 0; tid < numThreads; tid++)
        renameMap[tid].setFreeList(&freeList);
    rename.setFreeList(&freeList);

    // Setup the ROB for whichever stages need it.
    commit.setROB(&rob);

    lastRunningCycle = curTick;

    lastActivatedCycle = -1;
#if 0
    // Give renameMap & rename stage access to the freeList;
    for (ThreadID tid = 0; tid < numThreads; tid++)
        globalSeqNum[tid] = 1;
#endif

    contextSwitch = false;
    DPRINTF(O3CPU, "Creating O3CPU object.\n");

    // Setup any thread state.
    this->thread.resize(this->numThreads);

    for (ThreadID tid = 0; tid < this->numThreads; ++tid) {
#if FULL_SYSTEM
        // SMT is not supported in FS mode yet.
        assert(this->numThreads == 1);
        this->thread[tid] = new Thread(this, 0);
#else
        if (tid < params->workload.size()) {
            DPRINTF(O3CPU, "Workload[%i] process is %#x",
                    tid, this->thread[tid]);
            this->thread[tid] = new typename FullO3CPU<Impl>::Thread(
                    (typename Impl::O3CPU *)(this),
                    tid, params->workload[tid]);

            //usedTids[tid] = true;
            //threadMap[tid] = tid;
        } else {
            //Allocate Empty thread so M5 can use later
            //when scheduling threads to CPU
            Process* dummy_proc = NULL;

            this->thread[tid] = new typename FullO3CPU<Impl>::Thread(
                    (typename Impl::O3CPU *)(this),
                    tid, dummy_proc);
            //usedTids[tid] = false;
        }
#endif // !FULL_SYSTEM

        ThreadContext *tc;

        // Setup the TC that will serve as the interface to the threads/CPU.
        O3ThreadContext<Impl> *o3_tc = new O3ThreadContext<Impl>;

        tc = o3_tc;

        // If we're using a checker, then the TC should be the
        // CheckerThreadContext.
#if USE_CHECKER
        if (params->checker) {
            tc = new CheckerThreadContext<O3ThreadContext<Impl> >(
                o3_tc, this->checker);
        }
#endif

        o3_tc->cpu = (typename Impl::O3CPU *)(this);
        assert(o3_tc->cpu);
        o3_tc->thread = this->thread[tid];

#if FULL_SYSTEM
        // Setup quiesce event.
        this->thread[tid]->quiesceEvent = new EndQuiesceEvent(tc);
#endif
        // Give the thread the TC.
        this->thread[tid]->tc = tc;

        // Add the TC to the CPU's list of TC's.
        this->threadContexts.push_back(tc);
    }

    for (ThreadID tid = 0; tid < this->numThreads; tid++)
        this->thread[tid]->setFuncExeInst(0);

    lockAddr = 0;
    lockFlag = false;
}

template <class Impl>
FullO3CPU<Impl>::~FullO3CPU()
{
}

template <class Impl>
void
FullO3CPU<Impl>::regStats()
{
    BaseO3CPU::regStats();

    // Register any of the O3CPU's stats here.
    timesIdled
        .name(name() + ".timesIdled")
        .desc("Number of times that the entire CPU went into an idle state and"
              " unscheduled itself")
        .prereq(timesIdled);

    idleCycles
        .name(name() + ".idleCycles")
        .desc("Total number of cycles that the CPU has spent unscheduled due "
              "to idling")
        .prereq(idleCycles);

    // Number of Instructions simulated
    // --------------------------------
    // Should probably be in Base CPU but need templated
    // MaxThreads so put in here instead
    committedInsts
        .init(numThreads)
        .name(name() + ".committedInsts")
        .desc("Number of Instructions Simulated");

    totalCommittedInsts
        .name(name() + ".committedInsts_total")
        .desc("Number of Instructions Simulated");

    cpi
        .name(name() + ".cpi")
        .desc("CPI: Cycles Per Instruction")
        .precision(6);
    cpi = numCycles / committedInsts;

    totalCpi
        .name(name() + ".cpi_total")
        .desc("CPI: Total CPI of All Threads")
        .precision(6);
    totalCpi = numCycles / totalCommittedInsts;

    ipc
        .name(name() + ".ipc")
        .desc("IPC: Instructions Per Cycle")
        .precision(6);
    ipc =  committedInsts / numCycles;

    totalIpc
        .name(name() + ".ipc_total")
        .desc("IPC: Total IPC of All Threads")
        .precision(6);
    totalIpc =  totalCommittedInsts / numCycles;

    this->fetch.regStats();
    this->decode.regStats();
    this->rename.regStats();
    this->iew.regStats();
    this->commit.regStats();
}

template <class Impl>
Port *
FullO3CPU<Impl>::getPort(const std::string &if_name, int idx)
{
    if (if_name == "dcache_port")
        return iew.getDcachePort();
    else if (if_name == "icache_port")
        return fetch.getIcachePort();
    else
        panic("No Such Port\n");
}

template <class Impl>
void
FullO3CPU<Impl>::tick()
{
    DPRINTF(O3CPU, "\n\nFullO3CPU: Ticking main, FullO3CPU.\n");

    ++numCycles;

//    activity = false;

    //Tick each of the stages
    fetch.tick();

    decode.tick();

    rename.tick();

    iew.tick();

    commit.tick();

#if !FULL_SYSTEM
    doContextSwitch();
#endif

    // Now advance the time buffers
    timeBuffer.advance();

    fetchQueue.advance();
    decodeQueue.advance();
    renameQueue.advance();
    iewQueue.advance();

    activityRec.advance();

    if (removeInstsThisCycle) {
        cleanUpRemovedInsts();
    }

    if (!tickEvent.scheduled()) {
        if (_status == SwitchedOut ||
            getState() == SimObject::Drained) {
            DPRINTF(O3CPU, "Switched out!\n");
            // increment stat
            lastRunningCycle = curTick;
        } else if (!activityRec.active() || _status == Idle) {
            DPRINTF(O3CPU, "Idle!\n");
            lastRunningCycle = curTick;
            timesIdled++;
        } else {
            schedule(tickEvent, nextCycle(curTick + ticks(1)));
            DPRINTF(O3CPU, "Scheduling next tick!\n");
        }
    }

#if !FULL_SYSTEM
    updateThreadPriority();
#endif
}

template <class Impl>
void
FullO3CPU<Impl>::init()
{
    BaseCPU::init();

    // Set inSyscall so that the CPU doesn't squash when initially
    // setting up registers.
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        thread[tid]->inSyscall = true;

#if FULL_SYSTEM
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        ThreadContext *src_tc = threadContexts[tid];
        TheISA::initCPU(src_tc, src_tc->contextId());
    }
#endif

    // Clear inSyscall.
    for (int tid = 0; tid < numThreads; ++tid)
        thread[tid]->inSyscall = false;

    // Initialize stages.
    fetch.initStage();
    iew.initStage();
    rename.initStage();
    commit.initStage();

    commit.setThreads(thread);
}

template <class Impl>
void
FullO3CPU<Impl>::activateThread(ThreadID tid)
{
    list<ThreadID>::iterator isActive =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(O3CPU, "[tid:%i]: Calling activate thread.\n", tid);

    if (isActive == activeThreads.end()) {
        DPRINTF(O3CPU, "[tid:%i]: Adding to active threads list\n",
                tid);

        activeThreads.push_back(tid);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::deactivateThread(ThreadID tid)
{
    //Remove From Active List, if Active
    list<ThreadID>::iterator thread_it =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    DPRINTF(O3CPU, "[tid:%i]: Calling deactivate thread.\n", tid);

    if (thread_it != activeThreads.end()) {
        DPRINTF(O3CPU,"[tid:%i]: Removing from active threads list\n",
                tid);
        activeThreads.erase(thread_it);
    }
}

template <class Impl>
Counter
FullO3CPU<Impl>::totalInstructions() const
{
    Counter total(0);

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++)
        total += thread[i]->numInst;

    return total;
}

template <class Impl>
void
FullO3CPU<Impl>::activateContext(ThreadID tid, int delay)
{
    // Needs to set each stage to running as well.
    if (delay){
        DPRINTF(O3CPU, "[tid:%i]: Scheduling thread context to activate "
                "on cycle %d\n", tid, curTick + ticks(delay));
        scheduleActivateThreadEvent(tid, delay);
    } else {
        activateThread(tid);
    }

    if (lastActivatedCycle < curTick) {
        scheduleTickEvent(delay);

        // Be sure to signal that there's some activity so the CPU doesn't
        // deschedule itself.
        activityRec.activity();
        fetch.wakeFromQuiesce();

        lastActivatedCycle = curTick;

        _status = Running;
    }
}

template <class Impl>
bool
FullO3CPU<Impl>::deallocateContext(ThreadID tid, bool remove, int delay)
{
    // Schedule removal of thread data from CPU
    if (delay){
        DPRINTF(O3CPU, "[tid:%i]: Scheduling thread context to deallocate "
                "on cycle %d\n", tid, curTick + ticks(delay));
        scheduleDeallocateContextEvent(tid, remove, delay);
        return false;
    } else {
        deactivateThread(tid);
        if (remove)
            removeThread(tid);
        return true;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::suspendContext(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid: %i]: Suspending Thread Context.\n", tid);
    bool deallocated = deallocateContext(tid, false, 1);
    // If this was the last thread then unschedule the tick event.
    if ((activeThreads.size() == 1 && !deallocated) ||
        activeThreads.size() == 0)
        unscheduleTickEvent();
    _status = Idle;
}

template <class Impl>
void
FullO3CPU<Impl>::haltContext(ThreadID tid)
{
    //For now, this is the same as deallocate
    DPRINTF(O3CPU,"[tid:%i]: Halt Context called. Deallocating", tid);
    deallocateContext(tid, true, 1);
}

template <class Impl>
void
FullO3CPU<Impl>::insertThread(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid:%i] Initializing thread into CPU");
    // Will change now that the PC and thread state is internal to the CPU
    // and not in the ThreadContext.
#if FULL_SYSTEM
    ThreadContext *src_tc = system->threadContexts[tid];
#else
    ThreadContext *src_tc = tcBase(tid);
#endif

    //Bind Int Regs to Rename Map
    for (int ireg = 0; ireg < TheISA::NumIntRegs; ireg++) {
        PhysRegIndex phys_reg = freeList.getIntReg();

        renameMap[tid].setEntry(ireg,phys_reg);
        scoreboard.setReg(phys_reg);
    }

    //Bind Float Regs to Rename Map
    for (int freg = 0; freg < TheISA::NumFloatRegs; freg++) {
        PhysRegIndex phys_reg = freeList.getFloatReg();

        renameMap[tid].setEntry(freg,phys_reg);
        scoreboard.setReg(phys_reg);
    }

    //Copy Thread Data Into RegFile
    //this->copyFromTC(tid);

    //Set PC/NPC/NNPC
    setPC(src_tc->readPC(), tid);
    setNextPC(src_tc->readNextPC(), tid);
    setNextNPC(src_tc->readNextNPC(), tid);

    src_tc->setStatus(ThreadContext::Active);

    activateContext(tid,1);

    //Reset ROB/IQ/LSQ Entries
    commit.rob->resetEntries();
    iew.resetEntries();
}

template <class Impl>
void
FullO3CPU<Impl>::removeThread(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid:%i] Removing thread context from CPU.\n", tid);

    // Copy Thread Data From RegFile
    // If thread is suspended, it might be re-allocated
    // this->copyToTC(tid);


    // @todo: 2-27-2008: Fix how we free up rename mappings
    // here to alleviate the case for double-freeing registers
    // in SMT workloads.

    // Unbind Int Regs from Rename Map
    for (int ireg = 0; ireg < TheISA::NumIntRegs; ireg++) {
        PhysRegIndex phys_reg = renameMap[tid].lookup(ireg);

        scoreboard.unsetReg(phys_reg);
        freeList.addReg(phys_reg);
    }

    // Unbind Float Regs from Rename Map
    for (int freg = TheISA::NumIntRegs; freg < TheISA::NumFloatRegs; freg++) {
        PhysRegIndex phys_reg = renameMap[tid].lookup(freg);

        scoreboard.unsetReg(phys_reg);
        freeList.addReg(phys_reg);
    }

    // Squash Throughout Pipeline
    InstSeqNum squash_seq_num = commit.rob->readHeadInst(tid)->seqNum;
    fetch.squash(0, sizeof(TheISA::MachInst), 0, squash_seq_num, tid);
    decode.squash(tid);
    rename.squash(squash_seq_num, tid);
    iew.squash(tid);
    iew.ldstQueue.squash(squash_seq_num, tid);
    commit.rob->squash(squash_seq_num, tid);


    assert(iew.instQueue.getCount(tid) == 0);
    assert(iew.ldstQueue.getCount(tid) == 0);

    // Reset ROB/IQ/LSQ Entries

    // Commented out for now.  This should be possible to do by
    // telling all the pipeline stages to drain first, and then
    // checking until the drain completes.  Once the pipeline is
    // drained, call resetEntries(). - 10-09-06 ktlim
/*
    if (activeThreads.size() >= 1) {
        commit.rob->resetEntries();
        iew.resetEntries();
    }
*/
}


template <class Impl>
void
FullO3CPU<Impl>::activateWhenReady(ThreadID tid)
{
    DPRINTF(O3CPU,"[tid:%i]: Checking if resources are available for incoming"
            "(e.g. PhysRegs/ROB/IQ/LSQ) \n",
            tid);

    bool ready = true;

    if (freeList.numFreeIntRegs() >= TheISA::NumIntRegs) {
        DPRINTF(O3CPU,"[tid:%i] Suspending thread due to not enough "
                "Phys. Int. Regs.\n",
                tid);
        ready = false;
    } else if (freeList.numFreeFloatRegs() >= TheISA::NumFloatRegs) {
        DPRINTF(O3CPU,"[tid:%i] Suspending thread due to not enough "
                "Phys. Float. Regs.\n",
                tid);
        ready = false;
    } else if (commit.rob->numFreeEntries() >=
               commit.rob->entryAmount(activeThreads.size() + 1)) {
        DPRINTF(O3CPU,"[tid:%i] Suspending thread due to not enough "
                "ROB entries.\n",
                tid);
        ready = false;
    } else if (iew.instQueue.numFreeEntries() >=
               iew.instQueue.entryAmount(activeThreads.size() + 1)) {
        DPRINTF(O3CPU,"[tid:%i] Suspending thread due to not enough "
                "IQ entries.\n",
                tid);
        ready = false;
    } else if (iew.ldstQueue.numFreeEntries() >=
               iew.ldstQueue.entryAmount(activeThreads.size() + 1)) {
        DPRINTF(O3CPU,"[tid:%i] Suspending thread due to not enough "
                "LSQ entries.\n",
                tid);
        ready = false;
    }

    if (ready) {
        insertThread(tid);

        contextSwitch = false;

        cpuWaitList.remove(tid);
    } else {
        suspendContext(tid);

        //blocks fetch
        contextSwitch = true;

        //@todo: dont always add to waitlist
        //do waitlist
        cpuWaitList.push_back(tid);
    }
}

#if FULL_SYSTEM
template <class Impl>
Fault
FullO3CPU<Impl>::hwrei(ThreadID tid)
{
#if THE_ISA == ALPHA_ISA
    // Need to clear the lock flag upon returning from an interrupt.
    this->setMiscRegNoEffect(AlphaISA::MISCREG_LOCKFLAG, false, tid);

    this->thread[tid]->kernelStats->hwrei();

    // FIXME: XXX check for interrupts? XXX
#endif
    return NoFault;
}

template <class Impl>
bool
FullO3CPU<Impl>::simPalCheck(int palFunc, ThreadID tid)
{
#if THE_ISA == ALPHA_ISA
    if (this->thread[tid]->kernelStats)
        this->thread[tid]->kernelStats->callpal(palFunc,
                                                this->threadContexts[tid]);

    switch (palFunc) {
      case PAL::halt:
        halt();
        if (--System::numSystemsRunning == 0)
            exitSimLoop("all cpus halted");
        break;

      case PAL::bpt:
      case PAL::bugchk:
        if (this->system->breakpoint())
            return false;
        break;
    }
#endif
    return true;
}

template <class Impl>
Fault
FullO3CPU<Impl>::getInterrupts()
{
    // Check if there are any outstanding interrupts
    return this->interrupts->getInterrupt(this->threadContexts[0]);
}

template <class Impl>
void
FullO3CPU<Impl>::processInterrupts(Fault interrupt)
{
    // Check for interrupts here.  For now can copy the code that
    // exists within isa_fullsys_traits.hh.  Also assume that thread 0
    // is the one that handles the interrupts.
    // @todo: Possibly consolidate the interrupt checking code.
    // @todo: Allow other threads to handle interrupts.

    assert(interrupt != NoFault);
    this->interrupts->updateIntrInfo(this->threadContexts[0]);

    DPRINTF(O3CPU, "Interrupt %s being handled\n", interrupt->name());
    this->trap(interrupt, 0);
}

template <class Impl>
void
FullO3CPU<Impl>::updateMemPorts()
{
    // Update all ThreadContext's memory ports (Functional/Virtual
    // Ports)
    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; ++i)
        thread[i]->connectMemPorts(thread[i]->getTC());
}
#endif

template <class Impl>
void
FullO3CPU<Impl>::trap(Fault fault, ThreadID tid)
{
    // Pass the thread's TC into the invoke method.
    fault->invoke(this->threadContexts[tid]);
}

#if !FULL_SYSTEM

template <class Impl>
void
FullO3CPU<Impl>::syscall(int64_t callnum, ThreadID tid)
{
    DPRINTF(O3CPU, "[tid:%i] Executing syscall().\n\n", tid);

    DPRINTF(Activity,"Activity: syscall() called.\n");

    // Temporarily increase this by one to account for the syscall
    // instruction.
    ++(this->thread[tid]->funcExeInst);

    // Execute the actual syscall.
    this->thread[tid]->syscall(callnum);

    // Decrease funcExeInst by one as the normal commit will handle
    // incrementing it.
    --(this->thread[tid]->funcExeInst);
}

#endif

template <class Impl>
void
FullO3CPU<Impl>::serialize(std::ostream &os)
{
    SimObject::State so_state = SimObject::getState();
    SERIALIZE_ENUM(so_state);
    BaseCPU::serialize(os);
    nameOut(os, csprintf("%s.tickEvent", name()));
    tickEvent.serialize(os);

    // Use SimpleThread's ability to checkpoint to make it easier to
    // write out the registers.  Also make this static so it doesn't
    // get instantiated multiple times (causes a panic in statistics).
    static SimpleThread temp;

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++) {
        nameOut(os, csprintf("%s.xc.%i", name(), i));
        temp.copyTC(thread[i]->getTC());
        temp.serialize(os);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::unserialize(Checkpoint *cp, const std::string &section)
{
    SimObject::State so_state;
    UNSERIALIZE_ENUM(so_state);
    BaseCPU::unserialize(cp, section);
    tickEvent.unserialize(cp, csprintf("%s.tickEvent", section));

    // Use SimpleThread's ability to checkpoint to make it easier to
    // read in the registers.  Also make this static so it doesn't
    // get instantiated multiple times (causes a panic in statistics).
    static SimpleThread temp;

    ThreadID size = thread.size();
    for (ThreadID i = 0; i < size; i++) {
        temp.copyTC(thread[i]->getTC());
        temp.unserialize(cp, csprintf("%s.xc.%i", section, i));
        thread[i]->getTC()->copyArchRegs(temp.getTC());
    }
}

template <class Impl>
unsigned int
FullO3CPU<Impl>::drain(Event *drain_event)
{
    DPRINTF(O3CPU, "Switching out\n");

    // If the CPU isn't doing anything, then return immediately.
    if (_status == Idle || _status == SwitchedOut) {
        return 0;
    }

    drainCount = 0;
    fetch.drain();
    decode.drain();
    rename.drain();
    iew.drain();
    commit.drain();

    // Wake the CPU and record activity so everything can drain out if
    // the CPU was not able to immediately drain.
    if (getState() != SimObject::Drained) {
        // A bit of a hack...set the drainEvent after all the drain()
        // calls have been made, that way if all of the stages drain
        // immediately, the signalDrained() function knows not to call
        // process on the drain event.
        drainEvent = drain_event;

        wakeCPU();
        activityRec.activity();

        return 1;
    } else {
        return 0;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::resume()
{
    fetch.resume();
    decode.resume();
    rename.resume();
    iew.resume();
    commit.resume();

    changeState(SimObject::Running);

    if (_status == SwitchedOut || _status == Idle)
        return;

#if FULL_SYSTEM
    assert(system->getMemoryMode() == Enums::timing);
#endif

    if (!tickEvent.scheduled())
        schedule(tickEvent, nextCycle());
    _status = Running;
}

template <class Impl>
void
FullO3CPU<Impl>::signalDrained()
{
    if (++drainCount == NumStages) {
        if (tickEvent.scheduled())
            tickEvent.squash();

        changeState(SimObject::Drained);

        BaseCPU::switchOut();

        if (drainEvent) {
            drainEvent->process();
            drainEvent = NULL;
        }
    }
    assert(drainCount <= 5);
}

template <class Impl>
void
FullO3CPU<Impl>::switchOut()
{
    fetch.switchOut();
    rename.switchOut();
    iew.switchOut();
    commit.switchOut();
    instList.clear();
    while (!removeList.empty()) {
        removeList.pop();
    }

    _status = SwitchedOut;
#if USE_CHECKER
    if (checker)
        checker->switchOut();
#endif
    if (tickEvent.scheduled())
        tickEvent.squash();
}

template <class Impl>
void
FullO3CPU<Impl>::takeOverFrom(BaseCPU *oldCPU)
{
    // Flush out any old data from the time buffers.
    for (int i = 0; i < timeBuffer.getSize(); ++i) {
        timeBuffer.advance();
        fetchQueue.advance();
        decodeQueue.advance();
        renameQueue.advance();
        iewQueue.advance();
    }

    activityRec.reset();

    BaseCPU::takeOverFrom(oldCPU, fetch.getIcachePort(), iew.getDcachePort());

    fetch.takeOverFrom();
    decode.takeOverFrom();
    rename.takeOverFrom();
    iew.takeOverFrom();
    commit.takeOverFrom();

    assert(!tickEvent.scheduled() || tickEvent.squashed());

    // @todo: Figure out how to properly select the tid to put onto
    // the active threads list.
    ThreadID tid = 0;

    list<ThreadID>::iterator isActive =
        std::find(activeThreads.begin(), activeThreads.end(), tid);

    if (isActive == activeThreads.end()) {
        //May Need to Re-code this if the delay variable is the delay
        //needed for thread to activate
        DPRINTF(O3CPU, "Adding Thread %i to active threads list\n",
                tid);

        activeThreads.push_back(tid);
    }

    // Set all statuses to active, schedule the CPU's tick event.
    // @todo: Fix up statuses so this is handled properly
    ThreadID size = threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *tc = threadContexts[i];
        if (tc->status() == ThreadContext::Active && _status != Running) {
            _status = Running;
            reschedule(tickEvent, nextCycle(), true);
        }
    }
    if (!tickEvent.scheduled())
        schedule(tickEvent, nextCycle());
}

template <class Impl>
TheISA::MiscReg
FullO3CPU<Impl>::readMiscRegNoEffect(int misc_reg, ThreadID tid)
{
    return this->isa[tid].readMiscRegNoEffect(misc_reg);
}

template <class Impl>
TheISA::MiscReg
FullO3CPU<Impl>::readMiscReg(int misc_reg, ThreadID tid)
{
    return this->isa[tid].readMiscReg(misc_reg, tcBase(tid));
}

template <class Impl>
void
FullO3CPU<Impl>::setMiscRegNoEffect(int misc_reg,
        const TheISA::MiscReg &val, ThreadID tid)
{
    this->isa[tid].setMiscRegNoEffect(misc_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setMiscReg(int misc_reg,
        const TheISA::MiscReg &val, ThreadID tid)
{
    this->isa[tid].setMiscReg(misc_reg, val, tcBase(tid));
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readIntReg(int reg_idx)
{
    return regFile.readIntReg(reg_idx);
}

template <class Impl>
FloatReg
FullO3CPU<Impl>::readFloatReg(int reg_idx)
{
    return regFile.readFloatReg(reg_idx);
}

template <class Impl>
FloatRegBits
FullO3CPU<Impl>::readFloatRegBits(int reg_idx)
{
    return regFile.readFloatRegBits(reg_idx);
}

template <class Impl>
void
FullO3CPU<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    regFile.setIntReg(reg_idx, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setFloatReg(int reg_idx, FloatReg val)
{
    regFile.setFloatReg(reg_idx, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setFloatRegBits(int reg_idx, FloatRegBits val)
{
    regFile.setFloatRegBits(reg_idx, val);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readArchIntReg(int reg_idx, ThreadID tid)
{
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(reg_idx);

    return regFile.readIntReg(phys_reg);
}

template <class Impl>
float
FullO3CPU<Impl>::readArchFloatReg(int reg_idx, ThreadID tid)
{
    int idx = reg_idx + TheISA::NumIntRegs;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(idx);

    return regFile.readFloatReg(phys_reg);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readArchFloatRegInt(int reg_idx, ThreadID tid)
{
    int idx = reg_idx + TheISA::NumIntRegs;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(idx);

    return regFile.readFloatRegBits(phys_reg);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchIntReg(int reg_idx, uint64_t val, ThreadID tid)
{
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(reg_idx);

    regFile.setIntReg(phys_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchFloatReg(int reg_idx, float val, ThreadID tid)
{
    int idx = reg_idx + TheISA::NumIntRegs;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(idx);

    regFile.setFloatReg(phys_reg, val);
}

template <class Impl>
void
FullO3CPU<Impl>::setArchFloatRegInt(int reg_idx, uint64_t val, ThreadID tid)
{
    int idx = reg_idx + TheISA::NumIntRegs;
    PhysRegIndex phys_reg = commitRenameMap[tid].lookup(idx);

    regFile.setFloatRegBits(phys_reg, val);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readPC(ThreadID tid)
{
    return commit.readPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::setPC(Addr new_PC, ThreadID tid)
{
    commit.setPC(new_PC, tid);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readMicroPC(ThreadID tid)
{
    return commit.readMicroPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::setMicroPC(Addr new_PC, ThreadID tid)
{
    commit.setMicroPC(new_PC, tid);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readNextPC(ThreadID tid)
{
    return commit.readNextPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::setNextPC(uint64_t val, ThreadID tid)
{
    commit.setNextPC(val, tid);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readNextNPC(ThreadID tid)
{
    return commit.readNextNPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::setNextNPC(uint64_t val, ThreadID tid)
{
    commit.setNextNPC(val, tid);
}

template <class Impl>
uint64_t
FullO3CPU<Impl>::readNextMicroPC(ThreadID tid)
{
    return commit.readNextMicroPC(tid);
}

template <class Impl>
void
FullO3CPU<Impl>::setNextMicroPC(Addr new_PC, ThreadID tid)
{
    commit.setNextMicroPC(new_PC, tid);
}

template <class Impl>
void
FullO3CPU<Impl>::squashFromTC(ThreadID tid)
{
    this->thread[tid]->inSyscall = true;
    this->commit.generateTCEvent(tid);
}

template <class Impl>
typename FullO3CPU<Impl>::ListIt
FullO3CPU<Impl>::addInst(DynInstPtr &inst)
{
    instList.push_back(inst);

    return --(instList.end());
}

template <class Impl>
void
FullO3CPU<Impl>::instDone(ThreadID tid)
{
    // Keep an instruction count.
    thread[tid]->numInst++;
    thread[tid]->numInsts++;
    committedInsts[tid]++;
    totalCommittedInsts++;

    // Check for instruction-count-based events.
    comInstEventQueue[tid]->serviceEvents(thread[tid]->numInst);
}

template <class Impl>
void
FullO3CPU<Impl>::addToRemoveList(DynInstPtr &inst)
{
    removeInstsThisCycle = true;

    removeList.push(inst->getInstListIt());
}

template <class Impl>
void
FullO3CPU<Impl>::removeFrontInst(DynInstPtr &inst)
{
    DPRINTF(O3CPU, "Removing committed instruction [tid:%i] PC %#x "
            "[sn:%lli]\n",
            inst->threadNumber, inst->readPC(), inst->seqNum);

    removeInstsThisCycle = true;

    // Remove the front instruction.
    removeList.push(inst->getInstListIt());
}

template <class Impl>
void
FullO3CPU<Impl>::removeInstsNotInROB(ThreadID tid)
{
    DPRINTF(O3CPU, "Thread %i: Deleting instructions from instruction"
            " list.\n", tid);

    ListIt end_it;

    bool rob_empty = false;

    if (instList.empty()) {
        return;
    } else if (rob.isEmpty(/*tid*/)) {
        DPRINTF(O3CPU, "ROB is empty, squashing all insts.\n");
        end_it = instList.begin();
        rob_empty = true;
    } else {
        end_it = (rob.readTailInst(tid))->getInstListIt();
        DPRINTF(O3CPU, "ROB is not empty, squashing insts not in ROB.\n");
    }

    removeInstsThisCycle = true;

    ListIt inst_it = instList.end();

    inst_it--;

    // Walk through the instruction list, removing any instructions
    // that were inserted after the given instruction iterator, end_it.
    while (inst_it != end_it) {
        assert(!instList.empty());

        squashInstIt(inst_it, tid);

        inst_it--;
    }

    // If the ROB was empty, then we actually need to remove the first
    // instruction as well.
    if (rob_empty) {
        squashInstIt(inst_it, tid);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::removeInstsUntil(const InstSeqNum &seq_num, ThreadID tid)
{
    assert(!instList.empty());

    removeInstsThisCycle = true;

    ListIt inst_iter = instList.end();

    inst_iter--;

    DPRINTF(O3CPU, "Deleting instructions from instruction "
            "list that are from [tid:%i] and above [sn:%lli] (end=%lli).\n",
            tid, seq_num, (*inst_iter)->seqNum);

    while ((*inst_iter)->seqNum > seq_num) {

        bool break_loop = (inst_iter == instList.begin());

        squashInstIt(inst_iter, tid);

        inst_iter--;

        if (break_loop)
            break;
    }
}

template <class Impl>
inline void
FullO3CPU<Impl>::squashInstIt(const ListIt &instIt, ThreadID tid)
{
    if ((*instIt)->threadNumber == tid) {
        DPRINTF(O3CPU, "Squashing instruction, "
                "[tid:%i] [sn:%lli] PC %#x\n",
                (*instIt)->threadNumber,
                (*instIt)->seqNum,
                (*instIt)->readPC());

        // Mark it as squashed.
        (*instIt)->setSquashed();

        // @todo: Formulate a consistent method for deleting
        // instructions from the instruction list
        // Remove the instruction from the list.
        removeList.push(instIt);
    }
}

template <class Impl>
void
FullO3CPU<Impl>::cleanUpRemovedInsts()
{
    while (!removeList.empty()) {
        DPRINTF(O3CPU, "Removing instruction, "
                "[tid:%i] [sn:%lli] PC %#x\n",
                (*removeList.front())->threadNumber,
                (*removeList.front())->seqNum,
                (*removeList.front())->readPC());

        instList.erase(removeList.front());

        removeList.pop();
    }

    removeInstsThisCycle = false;
}
/*
template <class Impl>
void
FullO3CPU<Impl>::removeAllInsts()
{
    instList.clear();
}
*/
template <class Impl>
void
FullO3CPU<Impl>::dumpInsts()
{
    int num = 0;

    ListIt inst_list_it = instList.begin();

    cprintf("Dumping Instruction List\n");

    while (inst_list_it != instList.end()) {
        cprintf("Instruction:%i\nPC:%#x\n[tid:%i]\n[sn:%lli]\nIssued:%i\n"
                "Squashed:%i\n\n",
                num, (*inst_list_it)->readPC(), (*inst_list_it)->threadNumber,
                (*inst_list_it)->seqNum, (*inst_list_it)->isIssued(),
                (*inst_list_it)->isSquashed());
        inst_list_it++;
        ++num;
    }
}
/*
template <class Impl>
void
FullO3CPU<Impl>::wakeDependents(DynInstPtr &inst)
{
    iew.wakeDependents(inst);
}
*/
template <class Impl>
void
FullO3CPU<Impl>::wakeCPU()
{
    if (activityRec.active() || tickEvent.scheduled()) {
        DPRINTF(Activity, "CPU already running.\n");
        return;
    }

    DPRINTF(Activity, "Waking up CPU\n");

    idleCycles += tickToCycles((curTick - 1) - lastRunningCycle);
    numCycles += tickToCycles((curTick - 1) - lastRunningCycle);

    schedule(tickEvent, nextCycle());
}

#if FULL_SYSTEM
template <class Impl>
void
FullO3CPU<Impl>::wakeup()
{
    if (this->thread[0]->status() != ThreadContext::Suspended)
        return;

    this->wakeCPU();

    DPRINTF(Quiesce, "Suspended Processor woken\n");
    this->threadContexts[0]->activate();
}
#endif

template <class Impl>
ThreadID
FullO3CPU<Impl>::getFreeTid()
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (!tids[tid]) {
            tids[tid] = true;
            return tid;
        }
    }

    return InvalidThreadID;
}

template <class Impl>
void
FullO3CPU<Impl>::doContextSwitch()
{
    if (contextSwitch) {

        //ADD CODE TO DEACTIVE THREAD HERE (???)

        ThreadID size = cpuWaitList.size();
        for (ThreadID tid = 0; tid < size; tid++) {
            activateWhenReady(tid);
        }

        if (cpuWaitList.size() == 0)
            contextSwitch = true;
    }
}

template <class Impl>
void
FullO3CPU<Impl>::updateThreadPriority()
{
    if (activeThreads.size() > 1) {
        //DEFAULT TO ROUND ROBIN SCHEME
        //e.g. Move highest priority to end of thread list
        list<ThreadID>::iterator list_begin = activeThreads.begin();
        list<ThreadID>::iterator list_end   = activeThreads.end();

        unsigned high_thread = *list_begin;

        activeThreads.erase(list_begin);

        activeThreads.push_back(high_thread);
    }
}

// Forward declaration of FullO3CPU.
template class FullO3CPU<O3CPUImpl>;
