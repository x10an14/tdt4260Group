/*
 * Copyright (c) 2007 MIPS Technologies, Inc.
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
 * Authors: Korey Sewell
 *
 */

#ifndef __CPU_INORDER_THREAD_CONTEXT_HH__
#define __CPU_INORDER_THREAD_CONTEXT_HH__

#include "config/the_isa.hh"
#include "cpu/exetrace.hh"
#include "cpu/thread_context.hh"
#include "cpu/inorder/thread_state.hh"
#include "cpu/inorder/cpu.hh"

class TranslatingPort;

/**
 * Derived ThreadContext class for use with the InOrderCPU.  It
 * provides the interface for any external objects to access a
 * single thread's state and some general CPU state.  Any time
 * external objects try to update state through this interface,
 * the CPU will create an event to squash all in-flight
 * instructions in order to ensure state is maintained correctly.
 * It must be defined specifically for the InOrderCPU because
 * not all architectural state is located within the O3ThreadState
 * (such as the commit PC, and registers), and specific actions
 * must be taken when using this interface (such as squashing all
 * in-flight instructions when doing a write to this interface).
 */
class InOrderThreadContext : public ThreadContext
{
  public:
    InOrderThreadContext() { }

    /** Pointer to the CPU. */
    InOrderCPU *cpu;

    /** Pointer to the thread state that this TC corrseponds to. */
    InOrderThreadState *thread;

    /** Returns a pointer to the ITB. */
    /** @TODO: PERF: Should we bind this to a pointer in constructor? */
    TheISA::TLB *getITBPtr() { return cpu->getITBPtr(); }

    /** Returns a pointer to the DTB. */
    /** @TODO: PERF: Should we bind this to a pointer in constructor? */
    TheISA::TLB *getDTBPtr() { return cpu->getDTBPtr(); }

    System *getSystemPtr() { return cpu->system; }

    /** Returns a pointer to this CPU. */
    BaseCPU *getCpuPtr() { return cpu; }

    /** Returns a pointer to this CPU. */
    std::string getCpuName() { return cpu->name(); }

    /** Reads this CPU's ID. */
    int cpuId() { return cpu->cpuId(); }

    int contextId() { return thread->contextId(); }

    void setContextId(int id) { thread->setContextId(id); }

    /** Returns this thread's ID number. */
    int threadId() { return thread->threadId(); }
    void setThreadId(int id) { return thread->setThreadId(id); }

    uint64_t readMicroPC()
    { return 0; }

    void setMicroPC(uint64_t val) { };

    uint64_t readNextMicroPC()
    { return 0; }

    void setNextMicroPC(uint64_t val) { };

#if FULL_SYSTEM
    /** Returns a pointer to physical memory. */
    PhysicalMemory *getPhysMemPtr()
    { assert(0); return 0; /*return cpu->physmem;*/ }

    /** Returns a pointer to this thread's kernel statistics. */
    TheISA::Kernel::Statistics *getKernelStats()
    { return thread->kernelStats; }

    FunctionalPort *getPhysPort() { return thread->getPhysPort(); }

    VirtualPort *getVirtPort();

    void connectMemPorts(ThreadContext *tc)
    { thread->connectMemPorts(tc); }

    /** Dumps the function profiling information.
     * @todo: Implement.
     */
    void dumpFuncProfile();

    /** Reads the last tick that this thread was activated on. */
    Tick readLastActivate();
    /** Reads the last tick that this thread was suspended on. */
    Tick readLastSuspend();

    /** Clears the function profiling information. */
    void profileClear();

    /** Samples the function profiling information. */
    void profileSample();

    /** Returns pointer to the quiesce event. */
    EndQuiesceEvent *getQuiesceEvent()
    {
        return this->thread->quiesceEvent;
    }
#else
    TranslatingPort *getMemPort() { return thread->getMemPort(); }

    /** Returns a pointer to this thread's process. */
    Process *getProcessPtr() { return thread->getProcessPtr(); }
#endif

    /** Returns this thread's status. */
    Status status() const { return thread->status(); }

    /** Sets this thread's status. */
    void setStatus(Status new_status)
    { thread->setStatus(new_status); }

    /** Set the status to Active.  Optional delay indicates number of
     * cycles to wait before beginning execution. */
    void activate(int delay = 1);

    /** Set the status to Suspended. */
    void suspend(int delay = 0);

    /** Set the status to Halted. */
    void halt(int delay = 0);

    /** Takes over execution of a thread from another CPU. */
    void takeOverFrom(ThreadContext *old_context);

    /** Registers statistics associated with this TC. */
    void regStats(const std::string &name);

    /** Serializes state. */
    void serialize(std::ostream &os);

    /** Unserializes state. */
    void unserialize(Checkpoint *cp, const std::string &section);

    /** Returns this thread's ID number. */
    int getThreadNum() { return thread->readTid(); }

    /** Returns the instruction this thread is currently committing.
     *  Only used when an instruction faults.
     */
    TheISA::MachInst getInst();

    /** Copies the architectural registers from another TC into this TC. */
    void copyArchRegs(ThreadContext *src_tc);

    /** Resets all architectural registers to 0. */
    void clearArchRegs();

    /** Reads an integer register. */
    uint64_t readIntReg(int reg_idx);

    FloatReg readFloatReg(int reg_idx);

    FloatRegBits readFloatRegBits(int reg_idx);

    uint64_t readRegOtherThread(int misc_reg, ThreadID tid);

    /** Sets an integer register to a value. */
    void setIntReg(int reg_idx, uint64_t val);

    void setFloatReg(int reg_idx, FloatReg val);

    void setFloatRegBits(int reg_idx, FloatRegBits val);

    void setRegOtherThread(int misc_reg,
                                   const MiscReg &val,
                                   ThreadID tid);

    /** Reads this thread's PC. */
    uint64_t readPC()
    { return cpu->readPC(thread->readTid()); }

    /** Sets this thread's PC. */
    void setPC(uint64_t val);

    /** Reads this thread's next PC. */
    uint64_t readNextPC()
    { return cpu->readNextPC(thread->readTid()); }

    /** Sets this thread's next PC. */
    void setNextPC(uint64_t val);

    uint64_t readNextNPC()
    { return cpu->readNextNPC(thread->readTid()); }

    void setNextNPC(uint64_t val);

    /** Reads a miscellaneous register. */
    MiscReg readMiscRegNoEffect(int misc_reg)
    { return cpu->readMiscRegNoEffect(misc_reg, thread->readTid()); }

    /** Reads a misc. register, including any side-effects the
     * read might have as defined by the architecture. */
    MiscReg readMiscReg(int misc_reg)
    { return cpu->readMiscReg(misc_reg, thread->readTid()); }

    /** Sets a misc. register. */
    void setMiscRegNoEffect(int misc_reg, const MiscReg &val);

    /** Sets a misc. register, including any side-effects the
     * write might have as defined by the architecture. */
    void setMiscReg(int misc_reg, const MiscReg &val);

    int flattenIntIndex(int reg)
    { return cpu->isa[thread->readTid()].flattenIntIndex(reg); }

    int flattenFloatIndex(int reg)
    { return cpu->isa[thread->readTid()].flattenFloatIndex(reg); }

    void activateContext(int delay)
    { cpu->activateContext(thread->readTid(), delay); }

    void deallocateContext()
    { cpu->deallocateContext(thread->readTid()); }

    /** Returns the number of consecutive store conditional failures. */
    // @todo: Figure out where these store cond failures should go.
    unsigned readStCondFailures()
    { return thread->storeCondFailures; }

    /** Sets the number of consecutive store conditional failures. */
    void setStCondFailures(unsigned sc_failures)
    { thread->storeCondFailures = sc_failures; }

    // Only really makes sense for old CPU model.  Lots of code
    // outside the CPU still checks this function, so it will
    // always return false to keep everything working.
    /** Checks if the thread is misspeculating.  Because it is
     * very difficult to determine if the thread is
     * misspeculating, this is set as false. */
    bool misspeculating() { return false; }

#if !FULL_SYSTEM
    /** Executes a syscall in SE mode. */
    void syscall(int64_t callnum)
    { return cpu->syscall(callnum, thread->readTid()); }
#endif

    /** Reads the funcExeInst counter. */
    Counter readFuncExeInst() { return thread->funcExeInst; }

    void changeRegFileContext(unsigned param,
                                      unsigned val)
    { panic("Not supported!"); }
};

#endif
