/*
 * Copyright (c) 2010 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
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
 * Authors: Gabe Black
 */

#include "arch/arm/isa_traits.hh"
#include "arch/arm/miscregs.hh"
#include "arch/arm/nativetrace.hh"
#include "cpu/thread_context.hh"
#include "params/ArmNativeTrace.hh"

namespace Trace {

#if TRACING_ON
static const char *regNames[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "fp", "r12", "sp", "lr", "pc",
    "cpsr"
};
#endif

void
Trace::ArmNativeTrace::ThreadState::update(NativeTrace *parent)
{
    oldState = state[current];
    current = (current + 1) % 2;
    newState = state[current];

    memcpy(newState, oldState, sizeof(state[0]));

    uint32_t diffVector;
    parent->read(&diffVector, sizeof(diffVector));
    diffVector = ArmISA::gtoh(diffVector);

    int changes = 0;
    for (int i = 0; i < STATE_NUMVALS; i++) {
        if (diffVector & 0x1) {
            changed[i] = true;
            changes++;
        } else {
            changed[i] = false;
        }
        diffVector >>= 1;
    }

    uint32_t values[changes];
    parent->read(values, sizeof(values));
    int pos = 0;
    for (int i = 0; i < STATE_NUMVALS; i++) {
        if (changed[i]) {
            newState[i] = ArmISA::gtoh(values[pos++]);
            changed[i] = (newState[i] != oldState[i]);
        }
    }
}

void
Trace::ArmNativeTrace::ThreadState::update(ThreadContext *tc)
{
    oldState = state[current];
    current = (current + 1) % 2;
    newState = state[current];

    // Regular int regs
    for (int i = 0; i < 15; i++) {
        newState[i] = tc->readIntReg(i);
        changed[i] = (oldState[i] != newState[i]);
    }

    //R15, aliased with the PC
    newState[STATE_PC] = tc->readNextPC();
    changed[STATE_PC] = (newState[STATE_PC] != oldState[STATE_PC]);

    //CPSR
    newState[STATE_CPSR] = tc->readMiscReg(MISCREG_CPSR) |
                           tc->readIntReg(INTREG_CONDCODES);
    changed[STATE_CPSR] = (newState[STATE_CPSR] != oldState[STATE_CPSR]);
}

void
Trace::ArmNativeTrace::check(NativeTraceRecord *record)
{
    ThreadContext *tc = record->getThread();
    // This area is read only on the target. It can't stop there to tell us
    // what's going on, so we should skip over anything there also.
    if (tc->readNextPC() > 0xffff0000)
        return;
    nState.update(this);
    mState.update(tc);

    // If a syscall just happened native trace needs another tick
    if ((mState.oldState[STATE_PC] == nState.oldState[STATE_PC]) &&
            (mState.newState[STATE_PC] - 4 == nState.newState[STATE_PC])) {
            DPRINTF(ExecRegDelta, "Advancing to match PCs after syscall\n");
            nState.update(this);

    }

    bool errorFound = false;
    // Regular int regs
    for (int i = 0; i < STATE_NUMVALS; i++) {
        if (nState.changed[i] || mState.changed[i]) {
            const char *vergence = "  ";
            bool oldMatch = (mState.oldState[i] == nState.oldState[i]);
            bool newMatch = (mState.newState[i] == nState.newState[i]);
            if (oldMatch && newMatch) {
                // The more things change, the more they stay the same.
                continue;
            } else if (oldMatch && !newMatch) {
                vergence = "<>";
            } else if (!oldMatch && newMatch) {
                vergence = "><";
            }
            errorFound = true;
            if (!nState.changed[i]) {
                DPRINTF(ExecRegDelta, "%s [%5s] "\
                                      "Native:         %#010x         "\
                                      "M5:     %#010x => %#010x\n",
                                      vergence, regNames[i],
                                      nState.newState[i],
                                      mState.oldState[i], mState.newState[i]);
            } else if (!mState.changed[i]) {
                DPRINTF(ExecRegDelta, "%s [%5s] "\
                                      "Native: %#010x => %#010x "\
                                      "M5:             %#010x        \n",
                                      vergence, regNames[i],
                                      nState.oldState[i], nState.newState[i],
                                      mState.newState[i]);
            } else {
                DPRINTF(ExecRegDelta, "%s [%5s] "\
                                      "Native: %#010x => %#010x "\
                                      "M5:     %#010x => %#010x\n",
                                      vergence, regNames[i],
                                      nState.oldState[i], nState.newState[i],
                                      mState.oldState[i], mState.newState[i]);
            }
        }
    }
    if (errorFound) {
        StaticInstPtr inst = record->getStaticInst();
        assert(inst);
        bool ran = true;
        if (inst->isMicroop()) {
            ran = false;
            inst = record->getMacroStaticInst();
        }
        assert(inst);
        record->traceInst(inst, ran);

        bool pcError = (mState.newState[STATE_PC] !=
                        nState.newState[STATE_PC]);
        if (stopOnPCError && pcError)
            panic("Native trace detected an error in control flow!");
    }
}

} /* namespace Trace */

////////////////////////////////////////////////////////////////////////
//
//  ExeTracer Simulation Object
//
Trace::ArmNativeTrace *
ArmNativeTraceParams::create()
{
    return new Trace::ArmNativeTrace(this);
};
