/*
 * Copyright (c) 2009 ARM Limited
 * All rights reserved.
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
 * Authors: Ali Saidi
 */


#include "arch/arm/faults.hh"
#include "arch/arm/utility.hh"
#include "cpu/thread_context.hh"


namespace ArmISA {

void
initCPU(ThreadContext *tc, int cpuId)
{
    // Reset CP15?? What does that mean -- ali
    
    // FPEXC.EN = 0
    
    static Fault reset = new Reset;
    if (cpuId == 0)
        reset->invoke(tc);
}

uint64_t getArgument(ThreadContext *tc, int number, bool fp) {
#if FULL_SYSTEM
    panic("getArgument() not implemented for ARM!\n");
#else
    panic("getArgument() only implemented for FULL_SYSTEM\n");
    M5_DUMMY_RETURN
#endif
}

Fault 
setCp15Register(uint32_t &Rd, int CRn, int opc1, int CRm, int opc2)
{
   return new UnimpFault(csprintf("MCR CP15: CRn: %d opc1: %d CRm: %d opc1: %d\n", 
               CRn, opc1, CRm, opc2));     
}

Fault 
readCp15Register(uint32_t &Rd, int CRn, int opc1, int CRm, int opc2)
{
   return new UnimpFault(csprintf("MRC CP15: CRn: %d opc1: %d CRm: %d opc1: %d\n", 
           CRn, opc1, CRm, opc2));

}


}
