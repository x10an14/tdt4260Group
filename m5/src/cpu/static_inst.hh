/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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
 */

#ifndef __CPU_STATIC_INST_HH__
#define __CPU_STATIC_INST_HH__

#include <bitset>
#include <string>

#include "arch/isa_traits.hh"
#include "arch/utility.hh"
#include "config/the_isa.hh"
#include "base/bitfield.hh"
#include "base/hashmap.hh"
#include "base/misc.hh"
#include "base/refcnt.hh"
#include "base/types.hh"
#include "cpu/op_class.hh"
#include "sim/faults.hh"
#include "sim/faults.hh"

// forward declarations
struct AlphaSimpleImpl;
struct OzoneImpl;
struct SimpleImpl;
class ThreadContext;
class DynInst;
class Packet;

class O3CPUImpl;
template <class Impl> class BaseO3DynInst;
typedef BaseO3DynInst<O3CPUImpl> O3DynInst;
template <class Impl> class OzoneDynInst;
class InOrderDynInst;

class CheckerCPU;
class FastCPU;
class AtomicSimpleCPU;
class TimingSimpleCPU;
class InorderCPU;
class SymbolTable;
class AddrDecodePage;

namespace Trace {
    class InstRecord;
}

typedef uint16_t MicroPC;

static const MicroPC MicroPCRomBit = 1 << (sizeof(MicroPC) * 8 - 1);

static inline MicroPC
romMicroPC(MicroPC upc)
{
    return upc | MicroPCRomBit;
}

static inline MicroPC
normalMicroPC(MicroPC upc)
{
    return upc & ~MicroPCRomBit;
}

static inline bool
isRomMicroPC(MicroPC upc)
{
    return MicroPCRomBit & upc;
}

/**
 * Base, ISA-independent static instruction class.
 *
 * The main component of this class is the vector of flags and the
 * associated methods for reading them.  Any object that can rely
 * solely on these flags can process instructions without being
 * recompiled for multiple ISAs.
 */
class StaticInstBase : public RefCounted
{
  protected:

    /// Set of boolean static instruction properties.
    ///
    /// Notes:
    /// - The IsInteger and IsFloating flags are based on the class of
    /// registers accessed by the instruction.  Although most
    /// instructions will have exactly one of these two flags set, it
    /// is possible for an instruction to have neither (e.g., direct
    /// unconditional branches, memory barriers) or both (e.g., an
    /// FP/int conversion).
    /// - If IsMemRef is set, then exactly one of IsLoad or IsStore
    /// will be set.
    /// - If IsControl is set, then exactly one of IsDirectControl or
    /// IsIndirect Control will be set, and exactly one of
    /// IsCondControl or IsUncondControl will be set.
    /// - IsSerializing, IsMemBarrier, and IsWriteBarrier are
    /// implemented as flags since in the current model there's no
    /// other way for instructions to inject behavior into the
    /// pipeline outside of fetch.  Once we go to an exec-in-exec CPU
    /// model we should be able to get rid of these flags and
    /// implement this behavior via the execute() methods.
    ///
    enum Flags {
        IsNop,          ///< Is a no-op (no effect at all).

        IsInteger,      ///< References integer regs.
        IsFloating,     ///< References FP regs.

        IsMemRef,       ///< References memory (load, store, or prefetch).
        IsLoad,         ///< Reads from memory (load or prefetch).
        IsStore,        ///< Writes to memory.
        IsStoreConditional,    ///< Store conditional instruction.
        IsIndexed,      ///< Accesses memory with an indexed address computation
        IsInstPrefetch, ///< Instruction-cache prefetch.
        IsDataPrefetch, ///< Data-cache prefetch.
        IsCopy,         ///< Fast Cache block copy

        IsControl,              ///< Control transfer instruction.
        IsDirectControl,        ///< PC relative control transfer.
        IsIndirectControl,      ///< Register indirect control transfer.
        IsCondControl,          ///< Conditional control transfer.
        IsUncondControl,        ///< Unconditional control transfer.
        IsCall,                 ///< Subroutine call.
        IsReturn,               ///< Subroutine return.

        IsCondDelaySlot,///< Conditional Delay-Slot Instruction

        IsThreadSync,   ///< Thread synchronization operation.

        IsSerializing,  ///< Serializes pipeline: won't execute until all
                        /// older instructions have committed.
        IsSerializeBefore,
        IsSerializeAfter,
        IsMemBarrier,   ///< Is a memory barrier
        IsWriteBarrier, ///< Is a write barrier
        IsReadBarrier,  ///< Is a read barrier
        IsERET, /// <- Causes the IFU to stall (MIPS ISA)

        IsNonSpeculative, ///< Should not be executed speculatively
        IsQuiesce,      ///< Is a quiesce instruction

        IsIprAccess,    ///< Accesses IPRs
        IsUnverifiable, ///< Can't be verified by a checker

        IsSyscall,      ///< Causes a system call to be emulated in syscall
                        /// emulation mode.

        //Flags for microcode
        IsMacroop,      ///< Is a macroop containing microops
        IsMicroop,      ///< Is a microop
        IsDelayedCommit,        ///< This microop doesn't commit right away
        IsLastMicroop,  ///< This microop ends a microop sequence
        IsFirstMicroop, ///< This microop begins a microop sequence
        //This flag doesn't do anything yet
        IsMicroBranch,  ///< This microop branches within the microcode for a macroop
        IsDspOp,

        NumFlags
    };

    /// Flag values for this instruction.
    std::bitset<NumFlags> flags;

    /// See opClass().
    OpClass _opClass;

    /// See numSrcRegs().
    int8_t _numSrcRegs;

    /// See numDestRegs().
    int8_t _numDestRegs;

    /// The following are used to track physical register usage
    /// for machines with separate int & FP reg files.
    //@{
    int8_t _numFPDestRegs;
    int8_t _numIntDestRegs;
    //@}

    /// Constructor.
    /// It's important to initialize everything here to a sane
    /// default, since the decoder generally only overrides
    /// the fields that are meaningful for the particular
    /// instruction.
    StaticInstBase(OpClass __opClass)
        : _opClass(__opClass), _numSrcRegs(0), _numDestRegs(0),
          _numFPDestRegs(0), _numIntDestRegs(0)
    {
    }

  public:

    /// @name Register information.
    /// The sum of numFPDestRegs() and numIntDestRegs() equals
    /// numDestRegs().  The former two functions are used to track
    /// physical register usage for machines with separate int & FP
    /// reg files.
    //@{
    /// Number of source registers.
    int8_t numSrcRegs()  const { return _numSrcRegs; }
    /// Number of destination registers.
    int8_t numDestRegs() const { return _numDestRegs; }
    /// Number of floating-point destination regs.
    int8_t numFPDestRegs()  const { return _numFPDestRegs; }
    /// Number of integer destination regs.
    int8_t numIntDestRegs() const { return _numIntDestRegs; }
    //@}

    /// @name Flag accessors.
    /// These functions are used to access the values of the various
    /// instruction property flags.  See StaticInstBase::Flags for descriptions
    /// of the individual flags.
    //@{

    bool isNop()          const { return flags[IsNop]; }

    bool isMemRef()       const { return flags[IsMemRef]; }
    bool isLoad()         const { return flags[IsLoad]; }
    bool isStore()        const { return flags[IsStore]; }
    bool isStoreConditional()     const { return flags[IsStoreConditional]; }
    bool isInstPrefetch() const { return flags[IsInstPrefetch]; }
    bool isDataPrefetch() const { return flags[IsDataPrefetch]; }
    bool isCopy()         const { return flags[IsCopy];}

    bool isInteger()      const { return flags[IsInteger]; }
    bool isFloating()     const { return flags[IsFloating]; }

    bool isControl()      const { return flags[IsControl]; }
    bool isCall()         const { return flags[IsCall]; }
    bool isReturn()       const { return flags[IsReturn]; }
    bool isDirectCtrl()   const { return flags[IsDirectControl]; }
    bool isIndirectCtrl() const { return flags[IsIndirectControl]; }
    bool isCondCtrl()     const { return flags[IsCondControl]; }
    bool isUncondCtrl()   const { return flags[IsUncondControl]; }
    bool isCondDelaySlot() const { return flags[IsCondDelaySlot]; }

    bool isThreadSync()   const { return flags[IsThreadSync]; }
    bool isSerializing()  const { return flags[IsSerializing] ||
                                      flags[IsSerializeBefore] ||
                                      flags[IsSerializeAfter]; }
    bool isSerializeBefore() const { return flags[IsSerializeBefore]; }
    bool isSerializeAfter() const { return flags[IsSerializeAfter]; }
    bool isMemBarrier()   const { return flags[IsMemBarrier]; }
    bool isWriteBarrier() const { return flags[IsWriteBarrier]; }
    bool isNonSpeculative() const { return flags[IsNonSpeculative]; }
    bool isQuiesce() const { return flags[IsQuiesce]; }
    bool isIprAccess() const { return flags[IsIprAccess]; }
    bool isUnverifiable() const { return flags[IsUnverifiable]; }
    bool isSyscall() const { return flags[IsSyscall]; }
    bool isMacroop() const { return flags[IsMacroop]; }
    bool isMicroop() const { return flags[IsMicroop]; }
    bool isDelayedCommit() const { return flags[IsDelayedCommit]; }
    bool isLastMicroop() const { return flags[IsLastMicroop]; }
    bool isFirstMicroop() const { return flags[IsFirstMicroop]; }
    //This flag doesn't do anything yet
    bool isMicroBranch() const { return flags[IsMicroBranch]; }
    //@}

    void setLastMicroop() { flags[IsLastMicroop] = true; }
    /// Operation class.  Used to select appropriate function unit in issue.
    OpClass opClass()     const { return _opClass; }
};


// forward declaration
class StaticInstPtr;

/**
 * Generic yet ISA-dependent static instruction class.
 *
 * This class builds on StaticInstBase, defining fields and interfaces
 * that are generic across all ISAs but that differ in details
 * according to the specific ISA being used.
 */
class StaticInst : public StaticInstBase
{
  public:

    /// Binary machine instruction type.
    typedef TheISA::MachInst MachInst;
    /// Binary extended machine instruction type.
    typedef TheISA::ExtMachInst ExtMachInst;
    /// Logical register index type.
    typedef TheISA::RegIndex RegIndex;

    enum {
        MaxInstSrcRegs = TheISA::MaxInstSrcRegs,        //< Max source regs
        MaxInstDestRegs = TheISA::MaxInstDestRegs,      //< Max dest regs
    };


    /// Return logical index (architectural reg num) of i'th destination reg.
    /// Only the entries from 0 through numDestRegs()-1 are valid.
    RegIndex destRegIdx(int i) const { return _destRegIdx[i]; }

    /// Return logical index (architectural reg num) of i'th source reg.
    /// Only the entries from 0 through numSrcRegs()-1 are valid.
    RegIndex srcRegIdx(int i)  const { return _srcRegIdx[i]; }

    /// Pointer to a statically allocated "null" instruction object.
    /// Used to give eaCompInst() and memAccInst() something to return
    /// when called on non-memory instructions.
    static StaticInstPtr nullStaticInstPtr;

    /**
     * Memory references only: returns "fake" instruction representing
     * the effective address part of the memory operation.  Used to
     * obtain the dependence info (numSrcRegs and srcRegIdx[]) for
     * just the EA computation.
     */
    virtual const
    StaticInstPtr &eaCompInst() const { return nullStaticInstPtr; }

    /**
     * Memory references only: returns "fake" instruction representing
     * the memory access part of the memory operation.  Used to
     * obtain the dependence info (numSrcRegs and srcRegIdx[]) for
     * just the memory access (not the EA computation).
     */
    virtual const
    StaticInstPtr &memAccInst() const { return nullStaticInstPtr; }

    /// The binary machine instruction.
    const ExtMachInst machInst;

  protected:

    /// See destRegIdx().
    RegIndex _destRegIdx[MaxInstDestRegs];
    /// See srcRegIdx().
    RegIndex _srcRegIdx[MaxInstSrcRegs];

    /**
     * Base mnemonic (e.g., "add").  Used by generateDisassembly()
     * methods.  Also useful to readily identify instructions from
     * within the debugger when #cachedDisassembly has not been
     * initialized.
     */
    const char *mnemonic;

    /**
     * String representation of disassembly (lazily evaluated via
     * disassemble()).
     */
    mutable std::string *cachedDisassembly;

    /**
     * Internal function to generate disassembly string.
     */
    virtual std::string
    generateDisassembly(Addr pc, const SymbolTable *symtab) const = 0;

    /// Constructor.
    StaticInst(const char *_mnemonic, ExtMachInst _machInst, OpClass __opClass)
        : StaticInstBase(__opClass),
          machInst(_machInst), mnemonic(_mnemonic), cachedDisassembly(0)
    { }

  public:
    virtual ~StaticInst();

/**
 * The execute() signatures are auto-generated by scons based on the
 * set of CPU models we are compiling in today.
 */
#include "cpu/static_inst_exec_sigs.hh"

    /**
     * Return the microop that goes with a particular micropc. This should
     * only be defined/used in macroops which will contain microops
     */
    virtual StaticInstPtr fetchMicroop(MicroPC micropc);

    /**
     * Return the target address for a PC-relative branch.
     * Invalid if not a PC-relative branch (i.e. isDirectCtrl()
     * should be true).
     */
    virtual Addr branchTarget(Addr branchPC) const;

    /**
     * Return the target address for an indirect branch (jump).  The
     * register value is read from the supplied thread context, so
     * the result is valid only if the thread context is about to
     * execute the branch in question.  Invalid if not an indirect
     * branch (i.e. isIndirectCtrl() should be true).
     */
    virtual Addr branchTarget(ThreadContext *tc) const;

    /**
     * Return true if the instruction is a control transfer, and if so,
     * return the target address as well.
     */
    bool hasBranchTarget(Addr pc, ThreadContext *tc, Addr &tgt) const;

    /**
     * Return string representation of disassembled instruction.
     * The default version of this function will call the internal
     * virtual generateDisassembly() function to get the string,
     * then cache it in #cachedDisassembly.  If the disassembly
     * should not be cached, this function should be overridden directly.
     */
    virtual const std::string &disassemble(Addr pc,
        const SymbolTable *symtab = 0) const;

    /// Decoded instruction cache type.
    /// For now we're using a generic hash_map; this seems to work
    /// pretty well.
    typedef m5::hash_map<ExtMachInst, StaticInstPtr> DecodeCache;

    /// A cache of decoded instruction objects.
    static DecodeCache decodeCache;

    /**
     * Dump some basic stats on the decode cache hash map.
     * Only gets called if DECODE_CACHE_HASH_STATS is defined.
     */
    static void dumpDecodeCacheStats();

    /// Decode a machine instruction.
    /// @param mach_inst The binary instruction to decode.
    /// @retval A pointer to the corresponding StaticInst object.
    //This is defined as inlined below.
    static StaticInstPtr decode(ExtMachInst mach_inst, Addr addr);

    /// Return name of machine instruction
    std::string getName() { return mnemonic; }

    /// Decoded instruction cache type, for address decoding.
    /// A generic hash_map is used.
    typedef m5::hash_map<Addr, AddrDecodePage *> AddrDecodeCache;

    /// A cache of decoded instruction objects from addresses.
    static AddrDecodeCache addrDecodeCache;

    struct cacheElement
    {
        Addr page_addr;
        AddrDecodePage *decodePage;

        cacheElement() : decodePage(NULL) { }
    };

    /// An array of recently decoded instructions.
    // might not use an array if there is only two elements
    static struct cacheElement recentDecodes[2];

    /// Updates the recently decoded instructions entries
    /// @param page_addr The page address recently used.
    /// @param decodePage Pointer to decoding page containing the decoded
    ///                   instruction.
    static inline void
    updateCache(Addr page_addr, AddrDecodePage *decodePage)
    {
        recentDecodes[1].page_addr = recentDecodes[0].page_addr;
        recentDecodes[1].decodePage = recentDecodes[0].decodePage;
        recentDecodes[0].page_addr = page_addr;
        recentDecodes[0].decodePage = decodePage;
    }

    /// Searches the decoded instruction cache for instruction decoding.
    /// If it is not found, then we decode the instruction.
    /// Otherwise, we get the instruction from the cache and move it into
    /// the address-to-instruction decoding page.
    /// @param mach_inst The binary instruction to decode.
    /// @param addr The address that contained the binary instruction.
    /// @param decodePage Pointer to decoding page containing the instruction.
    /// @retval A pointer to the corresponding StaticInst object.
    //This is defined as inlined below.
    static StaticInstPtr searchCache(ExtMachInst mach_inst, Addr addr,
                                     AddrDecodePage *decodePage);
};

typedef RefCountingPtr<StaticInstBase> StaticInstBasePtr;

/// Reference-counted pointer to a StaticInst object.
/// This type should be used instead of "StaticInst *" so that
/// StaticInst objects can be properly reference-counted.
class StaticInstPtr : public RefCountingPtr<StaticInst>
{
  public:
    /// Constructor.
    StaticInstPtr()
        : RefCountingPtr<StaticInst>()
    {
    }

    /// Conversion from "StaticInst *".
    StaticInstPtr(StaticInst *p)
        : RefCountingPtr<StaticInst>(p)
    {
    }

    /// Copy constructor.
    StaticInstPtr(const StaticInstPtr &r)
        : RefCountingPtr<StaticInst>(r)
    {
    }

    /// Construct directly from machine instruction.
    /// Calls StaticInst::decode().
    explicit StaticInstPtr(TheISA::ExtMachInst mach_inst, Addr addr)
        : RefCountingPtr<StaticInst>(StaticInst::decode(mach_inst, addr))
    {
    }

    /// Convert to pointer to StaticInstBase class.
    operator const StaticInstBasePtr()
    {
        return this->get();
    }
};

/// A page of a list of decoded instructions from an address.
class AddrDecodePage
{
  typedef TheISA::ExtMachInst ExtMachInst;
  protected:
    StaticInstPtr instructions[TheISA::PageBytes];
    bool valid[TheISA::PageBytes];
    Addr lowerMask;

  public:
    /// Constructor
    AddrDecodePage()
    {
        lowerMask = TheISA::PageBytes - 1;
        memset(valid, 0, TheISA::PageBytes);
    }

    /// Checks if the instruction is already decoded and the machine
    /// instruction in the cache matches the current machine instruction
    /// related to the address
    /// @param mach_inst The binary instruction to check
    /// @param addr The address containing the instruction
    bool
    decoded(ExtMachInst mach_inst, Addr addr)
    {
        return (valid[addr & lowerMask] &&
                (instructions[addr & lowerMask]->machInst == mach_inst));
    }

    /// Returns the instruction object. decoded should be called first
    /// to check if the instruction is valid.
    /// @param addr The address of the instruction.
    /// @retval A pointer to the corresponding StaticInst object.
    StaticInstPtr
    getInst(Addr addr)
    {
        return instructions[addr & lowerMask];
    }

    /// Inserts a pointer to a StaticInst object into the list of decoded
    /// instructions on the page.
    /// @param addr The address of the instruction.
    /// @param si A pointer to the corresponding StaticInst object.
    void
    insert(Addr addr, StaticInstPtr &si)
    {
        instructions[addr & lowerMask] = si;
        valid[addr & lowerMask] = true;
    }
};


inline StaticInstPtr
StaticInst::decode(StaticInst::ExtMachInst mach_inst, Addr addr)
{
#ifdef DECODE_CACHE_HASH_STATS
    // Simple stats on decode hash_map.  Turns out the default
    // hash function is as good as anything I could come up with.
    const int dump_every_n = 10000000;
    static int decodes_til_dump = dump_every_n;

    if (--decodes_til_dump == 0) {
        dumpDecodeCacheStats();
        decodes_til_dump = dump_every_n;
    }
#endif

    Addr page_addr = addr & ~(TheISA::PageBytes - 1);

    // checks recently decoded addresses
    if (recentDecodes[0].decodePage &&
        page_addr == recentDecodes[0].page_addr) {
        if (recentDecodes[0].decodePage->decoded(mach_inst, addr))
            return recentDecodes[0].decodePage->getInst(addr);

        return searchCache(mach_inst, addr, recentDecodes[0].decodePage);
    }

    if (recentDecodes[1].decodePage &&
        page_addr == recentDecodes[1].page_addr) {
        if (recentDecodes[1].decodePage->decoded(mach_inst, addr))
            return recentDecodes[1].decodePage->getInst(addr);

        return searchCache(mach_inst, addr, recentDecodes[1].decodePage);
    }

    // searches the page containing the address to decode
    AddrDecodeCache::iterator iter = addrDecodeCache.find(page_addr);
    if (iter != addrDecodeCache.end()) {
        updateCache(page_addr, iter->second);
        if (iter->second->decoded(mach_inst, addr))
            return iter->second->getInst(addr);

        return searchCache(mach_inst, addr, iter->second);
    }

    // creates a new object for a page of decoded instructions
    AddrDecodePage *decodePage = new AddrDecodePage;
    addrDecodeCache[page_addr] = decodePage;
    updateCache(page_addr, decodePage);
    return searchCache(mach_inst, addr, decodePage);
}

inline StaticInstPtr
StaticInst::searchCache(ExtMachInst mach_inst, Addr addr,
                        AddrDecodePage *decodePage)
{
    DecodeCache::iterator iter = decodeCache.find(mach_inst);
    if (iter != decodeCache.end()) {
        decodePage->insert(addr, iter->second);
        return iter->second;
    }

    StaticInstPtr si = TheISA::decodeInst(mach_inst);
    decodePage->insert(addr, si);
    decodeCache[mach_inst] = si;
    return si;
}

#endif // __CPU_STATIC_INST_HH__
