/*
 * Copyright (c) 2001-2006 The Regents of The University of Michigan
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

#ifndef __CPU_SIMPLE_THREAD_HH__
#define __CPU_SIMPLE_THREAD_HH__

#include "arch/isa.hh"
#include "arch/isa_traits.hh"
#include "arch/registers.hh"
#include "arch/tlb.hh"
#include "arch/types.hh"
#include "base/types.hh"
#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "cpu/thread_context.hh"
#include "cpu/thread_state.hh"
#include "mem/request.hh"
#include "sim/byteswap.hh"
#include "sim/eventq.hh"
#include "sim/serialize.hh"

class BaseCPU;

#if FULL_SYSTEM

#include "sim/system.hh"

class FunctionProfile;
class ProfileNode;
class FunctionalPort;
class PhysicalPort;

namespace TheISA {
    namespace Kernel {
        class Statistics;
    };
};

#else // !FULL_SYSTEM

#include "sim/process.hh"
#include "mem/page_table.hh"
class TranslatingPort;

#endif // FULL_SYSTEM

/**
 * The SimpleThread object provides a combination of the ThreadState
 * object and the ThreadContext interface. It implements the
 * ThreadContext interface so that a ProxyThreadContext class can be
 * made using SimpleThread as the template parameter (see
 * thread_context.hh). It adds to the ThreadState object by adding all
 * the objects needed for simple functional execution, including a
 * simple architectural register file, and pointers to the ITB and DTB
 * in full system mode. For CPU models that do not need more advanced
 * ways to hold state (i.e. a separate physical register file, or
 * separate fetch and commit PC's), this SimpleThread class provides
 * all the necessary state for full architecture-level functional
 * simulation.  See the AtomicSimpleCPU or TimingSimpleCPU for
 * examples.
 */

class SimpleThread : public ThreadState
{
  protected:
    typedef TheISA::MachInst MachInst;
    typedef TheISA::MiscReg MiscReg;
    typedef TheISA::FloatReg FloatReg;
    typedef TheISA::FloatRegBits FloatRegBits;
  public:
    typedef ThreadContext::Status Status;

  protected:
    union {
        FloatReg f[TheISA::NumFloatRegs];
        FloatRegBits i[TheISA::NumFloatRegs];
    } floatRegs;
    TheISA::IntReg intRegs[TheISA::NumIntRegs];
    TheISA::ISA isa;    // one "instance" of the current ISA.

    /** The current microcode pc for the currently executing macro
     * operation.
     */
    MicroPC microPC;

    /** The next microcode pc for the currently executing macro
     * operation.
     */
    MicroPC nextMicroPC;

    /** The current pc.
     */
    Addr PC;

    /** The next pc.
     */
    Addr nextPC;

    /** The next next pc.
     */
    Addr nextNPC;

  public:
    // pointer to CPU associated with this SimpleThread
    BaseCPU *cpu;

    ProxyThreadContext<SimpleThread> *tc;

    System *system;

    TheISA::TLB *itb;
    TheISA::TLB *dtb;

    // constructor: initialize SimpleThread from given process structure
#if FULL_SYSTEM
    SimpleThread(BaseCPU *_cpu, int _thread_num, System *_system,
                 TheISA::TLB *_itb, TheISA::TLB *_dtb,
                 bool use_kernel_stats = true);
#else
    SimpleThread(BaseCPU *_cpu, int _thread_num, Process *_process,
                 TheISA::TLB *_itb, TheISA::TLB *_dtb);
#endif

    SimpleThread();

    virtual ~SimpleThread();

    virtual void takeOverFrom(ThreadContext *oldContext);

    void regStats(const std::string &name);

    void copyTC(ThreadContext *context);

    void copyState(ThreadContext *oldContext);

    void serialize(std::ostream &os);
    void unserialize(Checkpoint *cp, const std::string &section);

    /***************************************************************
     *  SimpleThread functions to provide CPU with access to various
     *  state.
     **************************************************************/

    /** Returns the pointer to this SimpleThread's ThreadContext. Used
     *  when a ThreadContext must be passed to objects outside of the
     *  CPU.
     */
    ThreadContext *getTC() { return tc; }

    void demapPage(Addr vaddr, uint64_t asn)
    {
        itb->demapPage(vaddr, asn);
        dtb->demapPage(vaddr, asn);
    }

    void demapInstPage(Addr vaddr, uint64_t asn)
    {
        itb->demapPage(vaddr, asn);
    }

    void demapDataPage(Addr vaddr, uint64_t asn)
    {
        dtb->demapPage(vaddr, asn);
    }

#if FULL_SYSTEM
    void dumpFuncProfile();

    Fault hwrei();

    bool simPalCheck(int palFunc);

#endif

    /*******************************************
     * ThreadContext interface functions.
     ******************************************/

    BaseCPU *getCpuPtr() { return cpu; }

    TheISA::TLB *getITBPtr() { return itb; }

    TheISA::TLB *getDTBPtr() { return dtb; }

    System *getSystemPtr() { return system; }

#if FULL_SYSTEM
    FunctionalPort *getPhysPort() { return physPort; }

    /** Return a virtual port. This port cannot be cached locally in an object.
     * After a CPU switch it may point to the wrong memory object which could
     * mean stale data.
     */
    VirtualPort *getVirtPort() { return virtPort; }
#endif

    Status status() const { return _status; }

    void setStatus(Status newStatus) { _status = newStatus; }

    /// Set the status to Active.  Optional delay indicates number of
    /// cycles to wait before beginning execution.
    void activate(int delay = 1);

    /// Set the status to Suspended.
    void suspend();

    /// Set the status to Halted.
    void halt();

    virtual bool misspeculating();

    Fault instRead(RequestPtr &req)
    {
        panic("instRead not implemented");
        // return funcPhysMem->read(req, inst);
        return NoFault;
    }

    void copyArchRegs(ThreadContext *tc);

    void clearArchRegs()
    {
        microPC = 0;
        nextMicroPC = 1;
        PC = nextPC = nextNPC = 0;
        memset(intRegs, 0, sizeof(intRegs));
        memset(floatRegs.i, 0, sizeof(floatRegs.i));
        isa.clear();
    }

    //
    // New accessors for new decoder.
    //
    uint64_t readIntReg(int reg_idx)
    {
        int flatIndex = isa.flattenIntIndex(reg_idx);
        assert(flatIndex < TheISA::NumIntRegs);
        uint64_t regVal = intRegs[flatIndex];
        DPRINTF(IntRegs, "Reading int reg %d as %#x.\n", reg_idx, regVal);
        return regVal;
    }

    FloatReg readFloatReg(int reg_idx)
    {
        int flatIndex = isa.flattenFloatIndex(reg_idx);
        assert(flatIndex < TheISA::NumFloatRegs);
        FloatReg regVal = floatRegs.f[flatIndex];
        DPRINTF(FloatRegs, "Reading float reg %d as %f, %#x.\n",
                reg_idx, regVal, floatRegs.i[flatIndex]);
        return regVal;
    }

    FloatRegBits readFloatRegBits(int reg_idx)
    {
        int flatIndex = isa.flattenFloatIndex(reg_idx);
        assert(flatIndex < TheISA::NumFloatRegs);
        FloatRegBits regVal = floatRegs.i[flatIndex];
        DPRINTF(FloatRegs, "Reading float reg %d bits as %#x, %f.\n",
                reg_idx, regVal, floatRegs.f[flatIndex]);
        return regVal;
    }

    void setIntReg(int reg_idx, uint64_t val)
    {
        int flatIndex = isa.flattenIntIndex(reg_idx);
        assert(flatIndex < TheISA::NumIntRegs);
        DPRINTF(IntRegs, "Setting int reg %d to %#x.\n", reg_idx, val);
        intRegs[flatIndex] = val;
    }

    void setFloatReg(int reg_idx, FloatReg val)
    {
        int flatIndex = isa.flattenFloatIndex(reg_idx);
        assert(flatIndex < TheISA::NumFloatRegs);
        floatRegs.f[flatIndex] = val;
        DPRINTF(FloatRegs, "Setting float reg %d to %f, %#x.\n",
                reg_idx, val, floatRegs.i[flatIndex]);
    }

    void setFloatRegBits(int reg_idx, FloatRegBits val)
    {
        int flatIndex = isa.flattenFloatIndex(reg_idx);
        assert(flatIndex < TheISA::NumFloatRegs);
        floatRegs.i[flatIndex] = val;
        DPRINTF(FloatRegs, "Setting float reg %d bits to %#x, %#f.\n",
                reg_idx, val, floatRegs.f[flatIndex]);
    }

    uint64_t readPC()
    {
        return PC;
    }

    void setPC(uint64_t val)
    {
        PC = val;
    }

    uint64_t readMicroPC()
    {
        return microPC;
    }

    void setMicroPC(uint64_t val)
    {
        microPC = val;
    }

    uint64_t readNextPC()
    {
        return nextPC;
    }

    void setNextPC(uint64_t val)
    {
        nextPC = val;
    }

    uint64_t readNextMicroPC()
    {
        return nextMicroPC;
    }

    void setNextMicroPC(uint64_t val)
    {
        nextMicroPC = val;
    }

    uint64_t readNextNPC()
    {
#if ISA_HAS_DELAY_SLOT
        return nextNPC;
#else
        return nextPC + sizeof(TheISA::MachInst);
#endif
    }

    void setNextNPC(uint64_t val)
    {
#if ISA_HAS_DELAY_SLOT
        nextNPC = val;
#endif
    }

    MiscReg
    readMiscRegNoEffect(int misc_reg, ThreadID tid = 0)
    {
        return isa.readMiscRegNoEffect(misc_reg);
    }

    MiscReg
    readMiscReg(int misc_reg, ThreadID tid = 0)
    {
        return isa.readMiscReg(misc_reg, tc);
    }

    void
    setMiscRegNoEffect(int misc_reg, const MiscReg &val, ThreadID tid = 0)
    {
        return isa.setMiscRegNoEffect(misc_reg, val);
    }

    void
    setMiscReg(int misc_reg, const MiscReg &val, ThreadID tid = 0)
    {
        return isa.setMiscReg(misc_reg, val, tc);
    }

    int
    flattenIntIndex(int reg)
    {
        return isa.flattenIntIndex(reg);
    }

    int
    flattenFloatIndex(int reg)
    {
        return isa.flattenFloatIndex(reg);
    }

    unsigned readStCondFailures() { return storeCondFailures; }

    void setStCondFailures(unsigned sc_failures)
    { storeCondFailures = sc_failures; }

#if !FULL_SYSTEM
    void syscall(int64_t callnum)
    {
        process->syscall(callnum, tc);
    }
#endif
};


// for non-speculative execution context, spec_mode is always false
inline bool
SimpleThread::misspeculating()
{
    return false;
}

#endif // __CPU_CPU_EXEC_CONTEXT_HH__
