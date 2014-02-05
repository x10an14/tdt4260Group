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

#include "arch/registers.hh"
#include "config/the_isa.hh"
#include "cpu/o3/thread_context.hh"
#include "cpu/quiesce_event.hh"

#if FULL_SYSTEM
template <class Impl>
VirtualPort *
O3ThreadContext<Impl>::getVirtPort()
{
    return thread->getVirtPort();
}

template <class Impl>
void
O3ThreadContext<Impl>::dumpFuncProfile()
{
    thread->dumpFuncProfile();
}
#endif

template <class Impl>
void
O3ThreadContext<Impl>::takeOverFrom(ThreadContext *old_context)
{
    // some things should already be set up
#if FULL_SYSTEM
    assert(getSystemPtr() == old_context->getSystemPtr());
#else
    assert(getProcessPtr() == old_context->getProcessPtr());
#endif

    // copy over functional state
    setStatus(old_context->status());
    copyArchRegs(old_context);
    setContextId(old_context->contextId());
    setThreadId(old_context->threadId());

#if !FULL_SYSTEM
    thread->funcExeInst = old_context->readFuncExeInst();
#else
    EndQuiesceEvent *other_quiesce = old_context->getQuiesceEvent();
    if (other_quiesce) {
        // Point the quiesce event's TC at this TC so that it wakes up
        // the proper CPU.
        other_quiesce->tc = this;
    }
    if (thread->quiesceEvent) {
        thread->quiesceEvent->tc = this;
    }

    // Transfer kernel stats from one CPU to the other.
    thread->kernelStats = old_context->getKernelStats();
//    storeCondFailures = 0;
    cpu->lockFlag = false;
#endif

    old_context->setStatus(ThreadContext::Halted);

    thread->inSyscall = false;
    thread->trapPending = false;
}

template <class Impl>
void
O3ThreadContext<Impl>::activate(int delay)
{
    DPRINTF(O3CPU, "Calling activate on Thread Context %d\n",
            threadId());

    if (thread->status() == ThreadContext::Active)
        return;

#if FULL_SYSTEM
    thread->lastActivate = curTick;
#endif

    thread->setStatus(ThreadContext::Active);

    // status() == Suspended
    cpu->activateContext(thread->threadId(), delay);
}

template <class Impl>
void
O3ThreadContext<Impl>::suspend(int delay)
{
    DPRINTF(O3CPU, "Calling suspend on Thread Context %d\n",
            threadId());

    if (thread->status() == ThreadContext::Suspended)
        return;

#if FULL_SYSTEM
    thread->lastActivate = curTick;
    thread->lastSuspend = curTick;
#endif
/*
#if FULL_SYSTEM
    // Don't change the status from active if there are pending interrupts
    if (cpu->checkInterrupts()) {
        assert(status() == ThreadContext::Active);
        return;
    }
#endif
*/
    thread->setStatus(ThreadContext::Suspended);
    cpu->suspendContext(thread->threadId());
}

template <class Impl>
void
O3ThreadContext<Impl>::halt(int delay)
{
    DPRINTF(O3CPU, "Calling halt on Thread Context %d\n",
            threadId());

    if (thread->status() == ThreadContext::Halted)
        return;

    thread->setStatus(ThreadContext::Halted);
    cpu->haltContext(thread->threadId());
}

template <class Impl>
void
O3ThreadContext<Impl>::regStats(const std::string &name)
{
#if FULL_SYSTEM
    thread->kernelStats = new TheISA::Kernel::Statistics(cpu->system);
    thread->kernelStats->regStats(name + ".kern");
#endif
}

template <class Impl>
void
O3ThreadContext<Impl>::serialize(std::ostream &os)
{
#if FULL_SYSTEM
    if (thread->kernelStats)
        thread->kernelStats->serialize(os);
#endif

}

template <class Impl>
void
O3ThreadContext<Impl>::unserialize(Checkpoint *cp, const std::string &section)
{
#if FULL_SYSTEM
    if (thread->kernelStats)
        thread->kernelStats->unserialize(cp, section);
#endif

}

#if FULL_SYSTEM
template <class Impl>
Tick
O3ThreadContext<Impl>::readLastActivate()
{
    return thread->lastActivate;
}

template <class Impl>
Tick
O3ThreadContext<Impl>::readLastSuspend()
{
    return thread->lastSuspend;
}

template <class Impl>
void
O3ThreadContext<Impl>::profileClear()
{
    thread->profileClear();
}

template <class Impl>
void
O3ThreadContext<Impl>::profileSample()
{
    thread->profileSample();
}
#endif

template <class Impl>
TheISA::MachInst
O3ThreadContext<Impl>:: getInst()
{
    return thread->getInst();
}

template <class Impl>
void
O3ThreadContext<Impl>::copyArchRegs(ThreadContext *tc)
{
    // This function will mess things up unless the ROB is empty and
    // there are no instructions in the pipeline.
    ThreadID tid = thread->threadId();
    PhysRegIndex renamed_reg;

    // First loop through the integer registers.
    for (int i = 0; i < TheISA::NumIntRegs; ++i) {
        renamed_reg = cpu->renameMap[tid].lookup(i);

        DPRINTF(O3CPU, "Copying over register %i, had data %lli, "
                "now has data %lli.\n",
                renamed_reg, cpu->readIntReg(renamed_reg),
                tc->readIntReg(i));

        cpu->setIntReg(renamed_reg, tc->readIntReg(i));
    }

    // Then loop through the floating point registers.
    for (int i = 0; i < TheISA::NumFloatRegs; ++i) {
        renamed_reg = cpu->renameMap[tid].lookup(i + TheISA::FP_Base_DepTag);
        cpu->setFloatRegBits(renamed_reg,
                             tc->readFloatRegBits(i));
    }

    // Copy the misc regs.
    TheISA::copyMiscRegs(tc, this);

    // Then finally set the PC, the next PC, the nextNPC, the micropc, and the
    // next micropc.
    cpu->setPC(tc->readPC(), tid);
    cpu->setNextPC(tc->readNextPC(), tid);
    cpu->setNextNPC(tc->readNextNPC(), tid);
    cpu->setMicroPC(tc->readMicroPC(), tid);
    cpu->setNextMicroPC(tc->readNextMicroPC(), tid);
#if !FULL_SYSTEM
    this->thread->funcExeInst = tc->readFuncExeInst();
#endif
}

template <class Impl>
void
O3ThreadContext<Impl>::clearArchRegs()
{}

template <class Impl>
uint64_t
O3ThreadContext<Impl>::readIntReg(int reg_idx)
{
    reg_idx = cpu->isa[thread->threadId()].flattenIntIndex(reg_idx);
    return cpu->readArchIntReg(reg_idx, thread->threadId());
}

template <class Impl>
TheISA::FloatReg
O3ThreadContext<Impl>::readFloatReg(int reg_idx)
{
    reg_idx = cpu->isa[thread->threadId()].flattenFloatIndex(reg_idx);
    return cpu->readArchFloatReg(reg_idx, thread->threadId());
}

template <class Impl>
TheISA::FloatRegBits
O3ThreadContext<Impl>::readFloatRegBits(int reg_idx)
{
    reg_idx = cpu->isa[thread->threadId()].flattenFloatIndex(reg_idx);
    return cpu->readArchFloatRegInt(reg_idx, thread->threadId());
}

template <class Impl>
void
O3ThreadContext<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    reg_idx = cpu->isa[thread->threadId()].flattenIntIndex(reg_idx);
    cpu->setArchIntReg(reg_idx, val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setFloatReg(int reg_idx, FloatReg val)
{
    reg_idx = cpu->isa[thread->threadId()].flattenFloatIndex(reg_idx);
    cpu->setArchFloatReg(reg_idx, val, thread->threadId());

    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setFloatRegBits(int reg_idx, FloatRegBits val)
{
    reg_idx = cpu->isa[thread->threadId()].flattenFloatIndex(reg_idx);
    cpu->setArchFloatRegInt(reg_idx, val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setPC(uint64_t val)
{
    cpu->setPC(val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setNextPC(uint64_t val)
{
    cpu->setNextPC(val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setMicroPC(uint64_t val)
{
    cpu->setMicroPC(val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setNextMicroPC(uint64_t val)
{
    cpu->setNextMicroPC(val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
int
O3ThreadContext<Impl>::flattenIntIndex(int reg)
{
    return cpu->isa[thread->threadId()].flattenIntIndex(reg);
}

template <class Impl>
int
O3ThreadContext<Impl>::flattenFloatIndex(int reg)
{
    return cpu->isa[thread->threadId()].flattenFloatIndex(reg);
}

template <class Impl>
void
O3ThreadContext<Impl>::setMiscRegNoEffect(int misc_reg, const MiscReg &val)
{
    cpu->setMiscRegNoEffect(misc_reg, val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

template <class Impl>
void
O3ThreadContext<Impl>::setMiscReg(int misc_reg,
                                                const MiscReg &val)
{
    cpu->setMiscReg(misc_reg, val, thread->threadId());

    // Squash if we're not already in a state update mode.
    if (!thread->trapPending && !thread->inSyscall) {
        cpu->squashFromTC(thread->threadId());
    }
}

