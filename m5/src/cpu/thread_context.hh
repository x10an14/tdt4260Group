/*
 * Copyright (c) 2006 The Regents of The University of Michigan
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

#ifndef __CPU_THREAD_CONTEXT_HH__
#define __CPU_THREAD_CONTEXT_HH__

#include "arch/registers.hh"
#include "arch/types.hh"
#include "base/types.hh"
#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "mem/request.hh"
#include "sim/byteswap.hh"
#include "sim/faults.hh"
#include "sim/serialize.hh"

// @todo: Figure out a more architecture independent way to obtain the ITB and
// DTB pointers.
namespace TheISA
{
    class TLB;
}
class BaseCPU;
class EndQuiesceEvent;
class Event;
class TranslatingPort;
class FunctionalPort;
class VirtualPort;
class Process;
class System;
namespace TheISA {
    namespace Kernel {
        class Statistics;
    };
};

/**
 * ThreadContext is the external interface to all thread state for
 * anything outside of the CPU. It provides all accessor methods to
 * state that might be needed by external objects, ranging from
 * register values to things such as kernel stats. It is an abstract
 * base class; the CPU can create its own ThreadContext by either
 * deriving from it, or using the templated ProxyThreadContext.
 *
 * The ThreadContext is slightly different than the ExecContext.  The
 * ThreadContext provides access to an individual thread's state; an
 * ExecContext provides ISA access to the CPU (meaning it is
 * implicitly multithreaded on SMT systems).  Additionally the
 * ThreadState is an abstract class that exactly defines the
 * interface; the ExecContext is a more implicit interface that must
 * be implemented so that the ISA can access whatever state it needs.
 */
class ThreadContext
{
  protected:
    typedef TheISA::MachInst MachInst;
    typedef TheISA::IntReg IntReg;
    typedef TheISA::FloatReg FloatReg;
    typedef TheISA::FloatRegBits FloatRegBits;
    typedef TheISA::MiscReg MiscReg;
  public:

    enum Status
    {
        /// Running.  Instructions should be executed only when
        /// the context is in this state.
        Active,

        /// Temporarily inactive.  Entered while waiting for
        /// synchronization, etc.
        Suspended,

        /// Permanently shut down.  Entered when target executes
        /// m5exit pseudo-instruction.  When all contexts enter
        /// this state, the simulation will terminate.
        Halted
    };

    virtual ~ThreadContext() { };

    virtual BaseCPU *getCpuPtr() = 0;

    virtual int cpuId() = 0;

    virtual int threadId() = 0;

    virtual void setThreadId(int id) = 0;

    virtual int contextId() = 0;

    virtual void setContextId(int id) = 0;

    virtual TheISA::TLB *getITBPtr() = 0;

    virtual TheISA::TLB *getDTBPtr() = 0;

    virtual System *getSystemPtr() = 0;

#if FULL_SYSTEM
    virtual TheISA::Kernel::Statistics *getKernelStats() = 0;

    virtual FunctionalPort *getPhysPort() = 0;

    virtual VirtualPort *getVirtPort() = 0;

    virtual void connectMemPorts(ThreadContext *tc) = 0;
#else
    virtual TranslatingPort *getMemPort() = 0;

    virtual Process *getProcessPtr() = 0;
#endif

    virtual Status status() const = 0;

    virtual void setStatus(Status new_status) = 0;

    /// Set the status to Active.  Optional delay indicates number of
    /// cycles to wait before beginning execution.
    virtual void activate(int delay = 1) = 0;

    /// Set the status to Suspended.
    virtual void suspend(int delay = 0) = 0;

    /// Set the status to Halted.
    virtual void halt(int delay = 0) = 0;

#if FULL_SYSTEM
    virtual void dumpFuncProfile() = 0;
#endif

    virtual void takeOverFrom(ThreadContext *old_context) = 0;

    virtual void regStats(const std::string &name) = 0;

    virtual void serialize(std::ostream &os) = 0;
    virtual void unserialize(Checkpoint *cp, const std::string &section) = 0;

#if FULL_SYSTEM
    virtual EndQuiesceEvent *getQuiesceEvent() = 0;

    // Not necessarily the best location for these...
    // Having an extra function just to read these is obnoxious
    virtual Tick readLastActivate() = 0;
    virtual Tick readLastSuspend() = 0;

    virtual void profileClear() = 0;
    virtual void profileSample() = 0;
#endif

    // Also somewhat obnoxious.  Really only used for the TLB fault.
    // However, may be quite useful in SPARC.
    virtual TheISA::MachInst getInst() = 0;

    virtual void copyArchRegs(ThreadContext *tc) = 0;

    virtual void clearArchRegs() = 0;

    //
    // New accessors for new decoder.
    //
    virtual uint64_t readIntReg(int reg_idx) = 0;

    virtual FloatReg readFloatReg(int reg_idx) = 0;

    virtual FloatRegBits readFloatRegBits(int reg_idx) = 0;

    virtual void setIntReg(int reg_idx, uint64_t val) = 0;

    virtual void setFloatReg(int reg_idx, FloatReg val) = 0;

    virtual void setFloatRegBits(int reg_idx, FloatRegBits val) = 0;

    virtual uint64_t readPC() = 0;

    virtual void setPC(uint64_t val) = 0;

    virtual uint64_t readNextPC() = 0;

    virtual void setNextPC(uint64_t val) = 0;

    virtual uint64_t readNextNPC() = 0;

    virtual void setNextNPC(uint64_t val) = 0;

    virtual uint64_t readMicroPC() = 0;

    virtual void setMicroPC(uint64_t val) = 0;

    virtual uint64_t readNextMicroPC() = 0;

    virtual void setNextMicroPC(uint64_t val) = 0;

    virtual MiscReg readMiscRegNoEffect(int misc_reg) = 0;

    virtual MiscReg readMiscReg(int misc_reg) = 0;

    virtual void setMiscRegNoEffect(int misc_reg, const MiscReg &val) = 0;

    virtual void setMiscReg(int misc_reg, const MiscReg &val) = 0;

    virtual int flattenIntIndex(int reg) = 0;
    virtual int flattenFloatIndex(int reg) = 0;

    virtual uint64_t
    readRegOtherThread(int misc_reg, ThreadID tid)
    {
        return 0;
    }

    virtual void
    setRegOtherThread(int misc_reg, const MiscReg &val, ThreadID tid)
    {
    }

    // Also not necessarily the best location for these two.  Hopefully will go
    // away once we decide upon where st cond failures goes.
    virtual unsigned readStCondFailures() = 0;

    virtual void setStCondFailures(unsigned sc_failures) = 0;

    // Only really makes sense for old CPU model.  Still could be useful though.
    virtual bool misspeculating() = 0;

#if !FULL_SYSTEM
    // Same with st cond failures.
    virtual Counter readFuncExeInst() = 0;

    virtual void syscall(int64_t callnum) = 0;

    // This function exits the thread context in the CPU and returns
    // 1 if the CPU has no more active threads (meaning it's OK to exit);
    // Used in syscall-emulation mode when a  thread calls the exit syscall.
    virtual int exit() { return 1; };
#endif

    /** function to compare two thread contexts (for debugging) */
    static void compare(ThreadContext *one, ThreadContext *two);
};

/**
 * ProxyThreadContext class that provides a way to implement a
 * ThreadContext without having to derive from it. ThreadContext is an
 * abstract class, so anything that derives from it and uses its
 * interface will pay the overhead of virtual function calls.  This
 * class is created to enable a user-defined Thread object to be used
 * wherever ThreadContexts are used, without paying the overhead of
 * virtual function calls when it is used by itself.  See
 * simple_thread.hh for an example of this.
 */
template <class TC>
class ProxyThreadContext : public ThreadContext
{
  public:
    ProxyThreadContext(TC *actual_tc)
    { actualTC = actual_tc; }

  private:
    TC *actualTC;

  public:

    BaseCPU *getCpuPtr() { return actualTC->getCpuPtr(); }

    int cpuId() { return actualTC->cpuId(); }

    int threadId() { return actualTC->threadId(); }

    void setThreadId(int id) { return actualTC->setThreadId(id); }

    int contextId() { return actualTC->contextId(); }

    void setContextId(int id) { actualTC->setContextId(id); }

    TheISA::TLB *getITBPtr() { return actualTC->getITBPtr(); }

    TheISA::TLB *getDTBPtr() { return actualTC->getDTBPtr(); }

    System *getSystemPtr() { return actualTC->getSystemPtr(); }

#if FULL_SYSTEM
    TheISA::Kernel::Statistics *getKernelStats()
    { return actualTC->getKernelStats(); }

    FunctionalPort *getPhysPort() { return actualTC->getPhysPort(); }

    VirtualPort *getVirtPort() { return actualTC->getVirtPort(); }

    void connectMemPorts(ThreadContext *tc) { actualTC->connectMemPorts(tc); }
#else
    TranslatingPort *getMemPort() { return actualTC->getMemPort(); }

    Process *getProcessPtr() { return actualTC->getProcessPtr(); }
#endif

    Status status() const { return actualTC->status(); }

    void setStatus(Status new_status) { actualTC->setStatus(new_status); }

    /// Set the status to Active.  Optional delay indicates number of
    /// cycles to wait before beginning execution.
    void activate(int delay = 1) { actualTC->activate(delay); }

    /// Set the status to Suspended.
    void suspend(int delay = 0) { actualTC->suspend(); }

    /// Set the status to Halted.
    void halt(int delay = 0) { actualTC->halt(); }

#if FULL_SYSTEM
    void dumpFuncProfile() { actualTC->dumpFuncProfile(); }
#endif

    void takeOverFrom(ThreadContext *oldContext)
    { actualTC->takeOverFrom(oldContext); }

    void regStats(const std::string &name) { actualTC->regStats(name); }

    void serialize(std::ostream &os) { actualTC->serialize(os); }
    void unserialize(Checkpoint *cp, const std::string &section)
    { actualTC->unserialize(cp, section); }

#if FULL_SYSTEM
    EndQuiesceEvent *getQuiesceEvent() { return actualTC->getQuiesceEvent(); }

    Tick readLastActivate() { return actualTC->readLastActivate(); }
    Tick readLastSuspend() { return actualTC->readLastSuspend(); }

    void profileClear() { return actualTC->profileClear(); }
    void profileSample() { return actualTC->profileSample(); }
#endif
    // @todo: Do I need this?
    MachInst getInst() { return actualTC->getInst(); }

    // @todo: Do I need this?
    void copyArchRegs(ThreadContext *tc) { actualTC->copyArchRegs(tc); }

    void clearArchRegs() { actualTC->clearArchRegs(); }

    //
    // New accessors for new decoder.
    //
    uint64_t readIntReg(int reg_idx)
    { return actualTC->readIntReg(reg_idx); }

    FloatReg readFloatReg(int reg_idx)
    { return actualTC->readFloatReg(reg_idx); }

    FloatRegBits readFloatRegBits(int reg_idx)
    { return actualTC->readFloatRegBits(reg_idx); }

    void setIntReg(int reg_idx, uint64_t val)
    { actualTC->setIntReg(reg_idx, val); }

    void setFloatReg(int reg_idx, FloatReg val)
    { actualTC->setFloatReg(reg_idx, val); }

    void setFloatRegBits(int reg_idx, FloatRegBits val)
    { actualTC->setFloatRegBits(reg_idx, val); }

    uint64_t readPC() { return actualTC->readPC(); }

    void setPC(uint64_t val) { actualTC->setPC(val); }

    uint64_t readNextPC() { return actualTC->readNextPC(); }

    void setNextPC(uint64_t val) { actualTC->setNextPC(val); }

    uint64_t readNextNPC() { return actualTC->readNextNPC(); }

    void setNextNPC(uint64_t val) { actualTC->setNextNPC(val); }

    uint64_t readMicroPC() { return actualTC->readMicroPC(); }

    void setMicroPC(uint64_t val) { actualTC->setMicroPC(val); }

    uint64_t readNextMicroPC() { return actualTC->readMicroPC(); }

    void setNextMicroPC(uint64_t val) { actualTC->setNextMicroPC(val); }

    MiscReg readMiscRegNoEffect(int misc_reg)
    { return actualTC->readMiscRegNoEffect(misc_reg); }

    MiscReg readMiscReg(int misc_reg)
    { return actualTC->readMiscReg(misc_reg); }

    void setMiscRegNoEffect(int misc_reg, const MiscReg &val)
    { return actualTC->setMiscRegNoEffect(misc_reg, val); }

    void setMiscReg(int misc_reg, const MiscReg &val)
    { return actualTC->setMiscReg(misc_reg, val); }

    int flattenIntIndex(int reg)
    { return actualTC->flattenIntIndex(reg); }

    int flattenFloatIndex(int reg)
    { return actualTC->flattenFloatIndex(reg); }

    unsigned readStCondFailures()
    { return actualTC->readStCondFailures(); }

    void setStCondFailures(unsigned sc_failures)
    { actualTC->setStCondFailures(sc_failures); }

    // @todo: Fix this!
    bool misspeculating() { return actualTC->misspeculating(); }

#if !FULL_SYSTEM
    void syscall(int64_t callnum)
    { actualTC->syscall(callnum); }

    Counter readFuncExeInst() { return actualTC->readFuncExeInst(); }
#endif
};

#endif
