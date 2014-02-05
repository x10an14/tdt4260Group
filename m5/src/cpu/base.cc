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

#include <iostream>
#include <string>
#include <sstream>

#include "base/cprintf.hh"
#include "base/loader/symtab.hh"
#include "base/misc.hh"
#include "base/output.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
#include "cpu/cpuevent.hh"
#include "cpu/thread_context.hh"
#include "cpu/profile.hh"
#include "params/BaseCPU.hh"
#include "sim/sim_exit.hh"
#include "sim/process.hh"
#include "sim/sim_events.hh"
#include "sim/system.hh"

// Hack
#include "sim/stat_control.hh"

using namespace std;

vector<BaseCPU *> BaseCPU::cpuList;

// This variable reflects the max number of threads in any CPU.  Be
// careful to only use it once all the CPUs that you care about have
// been initialized
int maxThreadsPerCPU = 1;

CPUProgressEvent::CPUProgressEvent(BaseCPU *_cpu, Tick ival)
    : Event(Event::Progress_Event_Pri), _interval(ival), lastNumInst(0),
      cpu(_cpu), _repeatEvent(true)
{
    if (_interval)
        cpu->schedule(this, curTick + _interval);
}

void
CPUProgressEvent::process()
{
    Counter temp = cpu->totalInstructions();
#ifndef NDEBUG
    double ipc = double(temp - lastNumInst) / (_interval / cpu->ticks(1));

    DPRINTFN("%s progress event, total committed:%i, progress insts committed: "
             "%lli, IPC: %0.8d\n", cpu->name(), temp, temp - lastNumInst,
             ipc);
    ipc = 0.0;
#else
    cprintf("%lli: %s progress event, total committed:%i, progress insts "
            "committed: %lli\n", curTick, cpu->name(), temp,
            temp - lastNumInst);
#endif
    lastNumInst = temp;

    if (_repeatEvent)
        cpu->schedule(this, curTick + _interval);
}

const char *
CPUProgressEvent::description() const
{
    return "CPU Progress";
}

#if FULL_SYSTEM
BaseCPU::BaseCPU(Params *p)
    : MemObject(p), clock(p->clock), instCnt(0), _cpuId(p->cpu_id),
      interrupts(p->interrupts),
      numThreads(p->numThreads), system(p->system),
      phase(p->phase)
#else
BaseCPU::BaseCPU(Params *p)
    : MemObject(p), clock(p->clock), _cpuId(p->cpu_id),
      numThreads(p->numThreads), system(p->system),
      phase(p->phase)
#endif
{
//    currentTick = curTick;

    // if Python did not provide a valid ID, do it here
    if (_cpuId == -1 ) {
        _cpuId = cpuList.size();
    }

    // add self to global list of CPUs
    cpuList.push_back(this);

    DPRINTF(SyscallVerbose, "Constructing CPU with id %d\n", _cpuId);

    if (numThreads > maxThreadsPerCPU)
        maxThreadsPerCPU = numThreads;

    // allocate per-thread instruction-based event queues
    comInstEventQueue = new EventQueue *[numThreads];
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        comInstEventQueue[tid] =
            new EventQueue("instruction-based event queue");

    //
    // set up instruction-count-based termination events, if any
    //
    if (p->max_insts_any_thread != 0) {
        const char *cause = "a thread reached the max instruction count";
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new SimLoopExitEvent(cause, 0);
            comInstEventQueue[tid]->schedule(event, p->max_insts_any_thread);
        }
    }

    if (p->max_insts_all_threads != 0) {
        const char *cause = "all threads reached the max instruction count";

        // allocate & initialize shared downcounter: each event will
        // decrement this when triggered; simulation will terminate
        // when counter reaches 0
        int *counter = new int;
        *counter = numThreads;
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new CountedExitEvent(cause, *counter);
            comInstEventQueue[tid]->schedule(event, p->max_insts_all_threads);
        }
    }

    // allocate per-thread load-based event queues
    comLoadEventQueue = new EventQueue *[numThreads];
    for (ThreadID tid = 0; tid < numThreads; ++tid)
        comLoadEventQueue[tid] = new EventQueue("load-based event queue");

    //
    // set up instruction-count-based termination events, if any
    //
    if (p->max_loads_any_thread != 0) {
        const char *cause = "a thread reached the max load count";
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new SimLoopExitEvent(cause, 0);
            comLoadEventQueue[tid]->schedule(event, p->max_loads_any_thread);
        }
    }

    if (p->max_loads_all_threads != 0) {
        const char *cause = "all threads reached the max load count";
        // allocate & initialize shared downcounter: each event will
        // decrement this when triggered; simulation will terminate
        // when counter reaches 0
        int *counter = new int;
        *counter = numThreads;
        for (ThreadID tid = 0; tid < numThreads; ++tid) {
            Event *event = new CountedExitEvent(cause, *counter);
            comLoadEventQueue[tid]->schedule(event, p->max_loads_all_threads);
        }
    }

    functionTracingEnabled = false;
    if (p->function_trace) {
        functionTraceStream = simout.find(csprintf("ftrace.%s", name()));
        currentFunctionStart = currentFunctionEnd = 0;
        functionEntryTick = p->function_trace_start;

        if (p->function_trace_start == 0) {
            functionTracingEnabled = true;
        } else {
            typedef EventWrapper<BaseCPU, &BaseCPU::enableFunctionTrace> wrap;
            Event *event = new wrap(this, true);
            schedule(event, p->function_trace_start);
        }
    }
#if FULL_SYSTEM
    interrupts->setCPU(this);

    profileEvent = NULL;
    if (params()->profile)
        profileEvent = new ProfileEvent(this, params()->profile);
#endif
    tracer = params()->tracer;
}

void
BaseCPU::enableFunctionTrace()
{
    functionTracingEnabled = true;
}

BaseCPU::~BaseCPU()
{
}

void
BaseCPU::init()
{
    if (!params()->defer_registration)
        registerThreadContexts();
}

void
BaseCPU::startup()
{
#if FULL_SYSTEM
    if (!params()->defer_registration && profileEvent)
        schedule(profileEvent, curTick);
#endif

    if (params()->progress_interval) {
        Tick num_ticks = ticks(params()->progress_interval);

        Event *event;
        event = new CPUProgressEvent(this, num_ticks);
    }
}


void
BaseCPU::regStats()
{
    using namespace Stats;

    numCycles
        .name(name() + ".numCycles")
        .desc("number of cpu cycles simulated")
        ;

    int size = threadContexts.size();
    if (size > 1) {
        for (int i = 0; i < size; ++i) {
            stringstream namestr;
            ccprintf(namestr, "%s.ctx%d", name(), i);
            threadContexts[i]->regStats(namestr.str());
        }
    } else if (size == 1)
        threadContexts[0]->regStats(name());

#if FULL_SYSTEM
#endif
}

Tick
BaseCPU::nextCycle()
{
    Tick next_tick = curTick - phase + clock - 1;
    next_tick -= (next_tick % clock);
    next_tick += phase;
    return next_tick;
}

Tick
BaseCPU::nextCycle(Tick begin_tick)
{
    Tick next_tick = begin_tick;
    if (next_tick % clock != 0)
        next_tick = next_tick - (next_tick % clock) + clock;
    next_tick += phase;

    assert(next_tick >= curTick);
    return next_tick;
}

void
BaseCPU::registerThreadContexts()
{
    ThreadID size = threadContexts.size();
    for (ThreadID tid = 0; tid < size; ++tid) {
        ThreadContext *tc = threadContexts[tid];

        /** This is so that contextId and cpuId match where there is a
         * 1cpu:1context relationship.  Otherwise, the order of registration
         * could affect the assignment and cpu 1 could have context id 3, for
         * example.  We may even want to do something like this for SMT so that
         * cpu 0 has the lowest thread contexts and cpu N has the highest, but
         * I'll just do this for now
         */
        if (numThreads == 1)
            tc->setContextId(system->registerThreadContext(tc, _cpuId));
        else
            tc->setContextId(system->registerThreadContext(tc));
#if !FULL_SYSTEM
        tc->getProcessPtr()->assignThreadContext(tc->contextId());
#endif
    }
}


int
BaseCPU::findContext(ThreadContext *tc)
{
    ThreadID size = threadContexts.size();
    for (ThreadID tid = 0; tid < size; ++tid) {
        if (tc == threadContexts[tid])
            return tid;
    }
    return 0;
}

void
BaseCPU::switchOut()
{
//    panic("This CPU doesn't support sampling!");
#if FULL_SYSTEM
    if (profileEvent && profileEvent->scheduled())
        deschedule(profileEvent);
#endif
}

void
BaseCPU::takeOverFrom(BaseCPU *oldCPU, Port *ic, Port *dc)
{
    assert(threadContexts.size() == oldCPU->threadContexts.size());

    _cpuId = oldCPU->cpuId();

    ThreadID size = threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *newTC = threadContexts[i];
        ThreadContext *oldTC = oldCPU->threadContexts[i];

        newTC->takeOverFrom(oldTC);

        CpuEvent::replaceThreadContext(oldTC, newTC);

        assert(newTC->contextId() == oldTC->contextId());
        assert(newTC->threadId() == oldTC->threadId());
        system->replaceThreadContext(newTC, newTC->contextId());

        /* This code no longer works since the zero register (e.g.,
         * r31 on Alpha) doesn't necessarily contain zero at this
         * point.
           if (DTRACE(Context))
            ThreadContext::compare(oldTC, newTC);
        */
    }

#if FULL_SYSTEM
    interrupts = oldCPU->interrupts;
    interrupts->setCPU(this);

    for (ThreadID i = 0; i < size; ++i)
        threadContexts[i]->profileClear();

    if (profileEvent)
        schedule(profileEvent, curTick);
#endif

    // Connect new CPU to old CPU's memory only if new CPU isn't
    // connected to anything.  Also connect old CPU's memory to new
    // CPU.
    if (!ic->isConnected()) {
        Port *peer = oldCPU->getPort("icache_port")->getPeer();
        ic->setPeer(peer);
        peer->setPeer(ic);
    }

    if (!dc->isConnected()) {
        Port *peer = oldCPU->getPort("dcache_port")->getPeer();
        dc->setPeer(peer);
        peer->setPeer(dc);
    }
}


#if FULL_SYSTEM
BaseCPU::ProfileEvent::ProfileEvent(BaseCPU *_cpu, Tick _interval)
    : cpu(_cpu), interval(_interval)
{ }

void
BaseCPU::ProfileEvent::process()
{
    ThreadID size = cpu->threadContexts.size();
    for (ThreadID i = 0; i < size; ++i) {
        ThreadContext *tc = cpu->threadContexts[i];
        tc->profileSample();
    }

    cpu->schedule(this, curTick + interval);
}

void
BaseCPU::serialize(std::ostream &os)
{
    SERIALIZE_SCALAR(instCnt);
    interrupts->serialize(os);
}

void
BaseCPU::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_SCALAR(instCnt);
    interrupts->unserialize(cp, section);
}

#endif // FULL_SYSTEM

void
BaseCPU::traceFunctionsInternal(Addr pc)
{
    if (!debugSymbolTable)
        return;

    // if pc enters different function, print new function symbol and
    // update saved range.  Otherwise do nothing.
    if (pc < currentFunctionStart || pc >= currentFunctionEnd) {
        string sym_str;
        bool found = debugSymbolTable->findNearestSymbol(pc, sym_str,
                                                         currentFunctionStart,
                                                         currentFunctionEnd);

        if (!found) {
            // no symbol found: use addr as label
            sym_str = csprintf("0x%x", pc);
            currentFunctionStart = pc;
            currentFunctionEnd = pc + 1;
        }

        ccprintf(*functionTraceStream, " (%d)\n%d: %s",
                 curTick - functionEntryTick, curTick, sym_str);
        functionEntryTick = curTick;
    }
}
