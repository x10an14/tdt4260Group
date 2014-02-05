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

#ifndef __CPU_THREAD_STATE_HH__
#define __CPU_THREAD_STATE_HH__

#include "arch/types.hh"
#include "config/the_isa.hh"
#include "cpu/profile.hh"
#include "cpu/thread_context.hh"
#include "cpu/base.hh"

#if !FULL_SYSTEM
#include "mem/mem_object.hh"
#include "sim/process.hh"
#endif

#if FULL_SYSTEM
class EndQuiesceEvent;
class FunctionProfile;
class ProfileNode;
namespace TheISA {
    namespace Kernel {
        class Statistics;
    };
};
#endif

class Checkpoint;
class Port;
class TranslatingPort;

/**
 *  Struct for holding general thread state that is needed across CPU
 *  models.  This includes things such as pointers to the process,
 *  memory, quiesce events, and certain stats.  This can be expanded
 *  to hold more thread-specific stats within it.
 */
struct ThreadState {
    typedef ThreadContext::Status Status;

#if FULL_SYSTEM
    ThreadState(BaseCPU *cpu, ThreadID _tid);
#else
    ThreadState(BaseCPU *cpu, ThreadID _tid, Process *_process);
#endif

    ~ThreadState();

    void serialize(std::ostream &os);

    void unserialize(Checkpoint *cp, const std::string &section);

    int cpuId() { return baseCpu->cpuId(); }

    int contextId() { return _contextId; }

    void setContextId(int id) { _contextId = id; }

    void setThreadId(ThreadID id) { _threadId = id; }

    ThreadID threadId() { return _threadId; }

    Tick readLastActivate() { return lastActivate; }

    Tick readLastSuspend() { return lastSuspend; }

#if FULL_SYSTEM
    void connectMemPorts(ThreadContext *tc);

    void connectPhysPort();

    void connectVirtPort(ThreadContext *tc);

    void dumpFuncProfile();

    EndQuiesceEvent *getQuiesceEvent() { return quiesceEvent; }

    void profileClear();

    void profileSample();

    TheISA::Kernel::Statistics *getKernelStats() { return kernelStats; }

    FunctionalPort *getPhysPort() { return physPort; }

    void setPhysPort(FunctionalPort *port) { physPort = port; }

    VirtualPort *getVirtPort() { return virtPort; }
#else
    Process *getProcessPtr() { return process; }

    TranslatingPort *getMemPort();

    void setMemPort(TranslatingPort *_port) { port = _port; }
#endif

    /** Sets the current instruction being committed. */
    void setInst(TheISA::MachInst _inst) { inst = _inst; }

    /** Returns the current instruction being committed. */
    TheISA::MachInst getInst() { return inst; }

    /** Reads the number of instructions functionally executed and
     * committed.
     */
    Counter readFuncExeInst() { return funcExeInst; }

    /** Sets the total number of instructions functionally executed
     * and committed.
     */
    void setFuncExeInst(Counter new_val) { funcExeInst = new_val; }

    /** Returns the status of this thread. */
    Status status() const { return _status; }

    /** Sets the status of this thread. */
    void setStatus(Status new_status) { _status = new_status; }

  public:
    /** Connects port to the functional port of the memory object
     * below the CPU. */
    void connectToMemFunc(Port *port);

    /** Number of instructions committed. */
    Counter numInst;
    /** Stat for number instructions committed. */
    Stats::Scalar numInsts;
    /** Stat for number of memory references. */
    Stats::Scalar numMemRefs;

    /** Number of simulated loads, used for tracking events based on
     * the number of loads committed.
     */
    Counter numLoad;

    /** The number of simulated loads committed prior to this run. */
    Counter startNumLoad;

  protected:
    ThreadContext::Status _status;

    // Pointer to the base CPU.
    BaseCPU *baseCpu;

    // system wide HW context id
    int _contextId;

    // Index of hardware thread context on the CPU that this represents.
    ThreadID _threadId;

  public:
    /** Last time activate was called on this thread. */
    Tick lastActivate;

    /** Last time suspend was called on this thread. */
    Tick lastSuspend;

#if FULL_SYSTEM
  public:
    FunctionProfile *profile;
    ProfileNode *profileNode;
    Addr profilePC;
    EndQuiesceEvent *quiesceEvent;

    TheISA::Kernel::Statistics *kernelStats;
  protected:
    /** A functional port outgoing only for functional accesses to physical
     * addresses.*/
    FunctionalPort *physPort;

    /** A functional port, outgoing only, for functional accesse to virtual
     * addresses. */
    VirtualPort *virtPort;
#else
    TranslatingPort *port;

    Process *process;
#endif

    /** Current instruction the thread is committing.  Only set and
     * used for DTB faults currently.
     */
    TheISA::MachInst inst;

  public:
    /**
     * Temporary storage to pass the source address from copy_load to
     * copy_store.
     * @todo Remove this temporary when we have a better way to do it.
     */
    Addr copySrcAddr;
    /**
     * Temp storage for the physical source address of a copy.
     * @todo Remove this temporary when we have a better way to do it.
     */
    Addr copySrcPhysAddr;

    /*
     * number of executed instructions, for matching with syscall trace
     * points in EIO files.
     */
    Counter funcExeInst;

    //
    // Count failed store conditionals so we can warn of apparent
    // application deadlock situations.
    unsigned storeCondFailures;
};

#endif // __CPU_THREAD_STATE_HH__
