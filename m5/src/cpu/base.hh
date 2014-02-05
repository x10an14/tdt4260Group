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
 * Authors: Steve Reinhardt
 *          Nathan Binkert
 */

#ifndef __CPU_BASE_HH__
#define __CPU_BASE_HH__

#include <vector>

#include "arch/isa_traits.hh"
#include "arch/microcode_rom.hh"
#include "base/statistics.hh"
#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "sim/eventq.hh"
#include "sim/insttracer.hh"
#include "mem/mem_object.hh"

#if FULL_SYSTEM
#include "arch/interrupts.hh"
#endif

class BaseCPUParams;
class BranchPred;
class CheckerCPU;
class ThreadContext;
class System;
class Port;

namespace TheISA
{
    class Predecoder;
}

class CPUProgressEvent : public Event
{
  protected:
    Tick _interval;
    Counter lastNumInst;
    BaseCPU *cpu;
    bool _repeatEvent;

  public:
    CPUProgressEvent(BaseCPU *_cpu, Tick ival = 0);

    void process();

    void interval(Tick ival) { _interval = ival; }
    Tick interval() { return _interval; }

    void repeatEvent(bool repeat) { _repeatEvent = repeat; }

    virtual const char *description() const;
};

class BaseCPU : public MemObject
{
  protected:
    // CPU's clock period in terms of the number of ticks of curTime.
    Tick clock;
    // @todo remove me after debugging with legion done
    Tick instCnt;
    // every cpu has an id, put it in the base cpu
    // Set at initialization, only time a cpuId might change is during a
    // takeover (which should be done from within the BaseCPU anyway,
    // therefore no setCpuId() method is provided
    int _cpuId;

  public:
    /** Reads this CPU's ID. */
    int cpuId() { return _cpuId; }

//    Tick currentTick;
    inline Tick frequency() const { return SimClock::Frequency / clock; }
    inline Tick ticks(int numCycles) const { return clock * numCycles; }
    inline Tick curCycle() const { return curTick / clock; }
    inline Tick tickToCycles(Tick val) const { return val / clock; }
    // @todo remove me after debugging with legion done
    Tick instCount() { return instCnt; }

    /** The next cycle the CPU should be scheduled, given a cache
     * access or quiesce event returning on this cycle.  This function
     * may return curTick if the CPU should run on the current cycle.
     */
    Tick nextCycle();

    /** The next cycle the CPU should be scheduled, given a cache
     * access or quiesce event returning on the given Tick.  This
     * function may return curTick if the CPU should run on the
     * current cycle.
     * @param begin_tick The tick that the event is completing on.
     */
    Tick nextCycle(Tick begin_tick);

    TheISA::MicrocodeRom microcodeRom;

#if FULL_SYSTEM
  protected:
    TheISA::Interrupts *interrupts;

  public:
    TheISA::Interrupts *
    getInterruptController()
    {
        return interrupts;
    }

    virtual void wakeup() = 0;

    void
    postInterrupt(int int_num, int index)
    {
        interrupts->post(int_num, index);
        wakeup();
    }

    void
    clearInterrupt(int int_num, int index)
    {
        interrupts->clear(int_num, index);
    }

    void
    clearInterrupts()
    {
        interrupts->clearAll();
    }

    bool
    checkInterrupts(ThreadContext *tc) const
    {
        return interrupts->checkInterrupts(tc);
    }

    class ProfileEvent : public Event
    {
      private:
        BaseCPU *cpu;
        Tick interval;

      public:
        ProfileEvent(BaseCPU *cpu, Tick interval);
        void process();
    };
    ProfileEvent *profileEvent;
#endif

  protected:
    std::vector<ThreadContext *> threadContexts;
    std::vector<TheISA::Predecoder *> predecoders;

    Trace::InstTracer * tracer;

  public:

    /// Provide access to the tracer pointer
    Trace::InstTracer * getTracer() { return tracer; }

    /// Notify the CPU that the indicated context is now active.  The
    /// delay parameter indicates the number of ticks to wait before
    /// executing (typically 0 or 1).
    virtual void activateContext(int thread_num, int delay) {}

    /// Notify the CPU that the indicated context is now suspended.
    virtual void suspendContext(int thread_num) {}

    /// Notify the CPU that the indicated context is now deallocated.
    virtual void deallocateContext(int thread_num) {}

    /// Notify the CPU that the indicated context is now halted.
    virtual void haltContext(int thread_num) {}

   /// Given a Thread Context pointer return the thread num
   int findContext(ThreadContext *tc);

   /// Given a thread num get tho thread context for it
   ThreadContext *getContext(int tn) { return threadContexts[tn]; }

  public:
    typedef BaseCPUParams Params;
    const Params *params() const
    { return reinterpret_cast<const Params *>(_params); }
    BaseCPU(Params *params);
    virtual ~BaseCPU();

    virtual void init();
    virtual void startup();
    virtual void regStats();

    virtual void activateWhenReady(ThreadID tid) {};

    void registerThreadContexts();

    /// Prepare for another CPU to take over execution.  When it is
    /// is ready (drained pipe) it signals the sampler.
    virtual void switchOut();

    /// Take over execution from the given CPU.  Used for warm-up and
    /// sampling.
    virtual void takeOverFrom(BaseCPU *, Port *ic, Port *dc);

    /**
     *  Number of threads we're actually simulating (<= SMT_MAX_THREADS).
     * This is a constant for the duration of the simulation.
     */
    ThreadID numThreads;

    TheISA::CoreSpecific coreParams; //ISA-Specific Params That Set Up State in Core

    /**
     * Vector of per-thread instruction-based event queues.  Used for
     * scheduling events based on number of instructions committed by
     * a particular thread.
     */
    EventQueue **comInstEventQueue;

    /**
     * Vector of per-thread load-based event queues.  Used for
     * scheduling events based on number of loads committed by
     *a particular thread.
     */
    EventQueue **comLoadEventQueue;

    System *system;

    Tick phase;

#if FULL_SYSTEM
    /**
     * Serialize this object to the given output stream.
     * @param os The stream to serialize to.
     */
    virtual void serialize(std::ostream &os);

    /**
     * Reconstruct the state of this object from a checkpoint.
     * @param cp The checkpoint use.
     * @param section The section name of this object
     */
    virtual void unserialize(Checkpoint *cp, const std::string &section);

#endif

    /**
     * Return pointer to CPU's branch predictor (NULL if none).
     * @return Branch predictor pointer.
     */
    virtual BranchPred *getBranchPred() { return NULL; };

    virtual Counter totalInstructions() const = 0;

    // Function tracing
  private:
    bool functionTracingEnabled;
    std::ostream *functionTraceStream;
    Addr currentFunctionStart;
    Addr currentFunctionEnd;
    Tick functionEntryTick;
    void enableFunctionTrace();
    void traceFunctionsInternal(Addr pc);

  protected:
    void traceFunctions(Addr pc)
    {
        if (functionTracingEnabled)
            traceFunctionsInternal(pc);
    }

  private:
    static std::vector<BaseCPU *> cpuList;   //!< Static global cpu list

  public:
    static int numSimulatedCPUs() { return cpuList.size(); }
    static Counter numSimulatedInstructions()
    {
        Counter total = 0;

        int size = cpuList.size();
        for (int i = 0; i < size; ++i)
            total += cpuList[i]->totalInstructions();

        return total;
    }

  public:
    // Number of CPU cycles simulated
    Stats::Scalar numCycles;
};

#endif // __CPU_BASE_HH__
