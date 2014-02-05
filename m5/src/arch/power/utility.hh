/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * Copyright (c) 2007-2008 The Florida State University
 * Copyright (c) 2009 The University of Edinburgh
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
 *          Stephen Hines
 *          Timothy M. Jones
 */

#ifndef __ARCH_POWER_UTILITY_HH__
#define __ARCH_POWER_UTILITY_HH__

#include "arch/power/miscregs.hh"
#include "arch/power/types.hh"
#include "base/hashmap.hh"
#include "base/types.hh"
#include "cpu/thread_context.hh"

namespace __hash_namespace {

template<>
struct hash<PowerISA::ExtMachInst> : public hash<uint32_t> {
    size_t operator()(const PowerISA::ExtMachInst &emi) const {
        return hash<uint32_t>::operator()((uint32_t)emi);
    };
};

} // __hash_namespace namespace

namespace PowerISA {

/**
 * Function to ensure ISA semantics about 0 registers.
 * @param tc The thread context.
 */
template <class TC>
void zeroRegisters(TC *tc);

// Instruction address compression hooks
static inline Addr
realPCToFetchPC(const Addr &addr)
{
    return addr;
}

static inline Addr
fetchPCToRealPC(const Addr &addr)
{
    return addr;
}

// the size of "fetched" instructions
static inline size_t
fetchInstSize()
{
    return sizeof(MachInst);
}

static inline MachInst
makeRegisterCopy(int dest, int src)
{
    panic("makeRegisterCopy not implemented");
    return 0;
}

inline void
startupCPU(ThreadContext *tc, int cpuId)
{
    tc->activate(0);
}

template <class XC>
Fault
checkFpEnableFault(XC *xc)
{
    return NoFault;
}

void
copyRegs(ThreadContext *src, ThreadContext *dest);

static inline void
copyMiscRegs(ThreadContext *src, ThreadContext *dest)
{
}

} // PowerISA namespace

#endif // __ARCH_POWER_UTILITY_HH__
