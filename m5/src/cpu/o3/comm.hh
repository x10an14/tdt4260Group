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
 */

#ifndef __CPU_O3_COMM_HH__
#define __CPU_O3_COMM_HH__

#include <vector>

#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "sim/faults.hh"

// Typedef for physical register index type. Although the Impl would be the
// most likely location for this, there are a few classes that need this
// typedef yet are not templated on the Impl. For now it will be defined here.
typedef short int PhysRegIndex;

/** Struct that defines the information passed from fetch to decode. */
template<class Impl>
struct DefaultFetchDefaultDecode {
    typedef typename Impl::DynInstPtr DynInstPtr;

    int size;

    DynInstPtr insts[Impl::MaxWidth];
    Fault fetchFault;
    InstSeqNum fetchFaultSN;
    bool clearFetchFault;
};

/** Struct that defines the information passed from decode to rename. */
template<class Impl>
struct DefaultDecodeDefaultRename {
    typedef typename Impl::DynInstPtr DynInstPtr;

    int size;

    DynInstPtr insts[Impl::MaxWidth];
};

/** Struct that defines the information passed from rename to IEW. */
template<class Impl>
struct DefaultRenameDefaultIEW {
    typedef typename Impl::DynInstPtr DynInstPtr;

    int size;

    DynInstPtr insts[Impl::MaxWidth];
};

/** Struct that defines the information passed from IEW to commit. */
template<class Impl>
struct DefaultIEWDefaultCommit {
    typedef typename Impl::DynInstPtr DynInstPtr;

    int size;

    DynInstPtr insts[Impl::MaxWidth];

    bool squash[Impl::MaxThreads];
    bool branchMispredict[Impl::MaxThreads];
    bool branchTaken[Impl::MaxThreads];
    Addr mispredPC[Impl::MaxThreads];
    Addr nextPC[Impl::MaxThreads];
    Addr nextNPC[Impl::MaxThreads];
    Addr nextMicroPC[Impl::MaxThreads];
    InstSeqNum squashedSeqNum[Impl::MaxThreads];

    bool includeSquashInst[Impl::MaxThreads];
};

template<class Impl>
struct IssueStruct {
    typedef typename Impl::DynInstPtr DynInstPtr;

    int size;

    DynInstPtr insts[Impl::MaxWidth];
};

/** Struct that defines all backwards communication. */
template<class Impl>
struct TimeBufStruct {
    struct decodeComm {
        bool squash;
        bool predIncorrect;
        uint64_t branchAddr;

        InstSeqNum doneSeqNum;

        // @todo: Might want to package this kind of branch stuff into a single
        // struct as it is used pretty frequently.
        bool branchMispredict;
        bool branchTaken;
        Addr mispredPC;
        Addr nextPC;
        Addr nextNPC;
        Addr nextMicroPC;

        unsigned branchCount;
    };

    decodeComm decodeInfo[Impl::MaxThreads];

    struct renameComm {
    };

    renameComm renameInfo[Impl::MaxThreads];

    struct iewComm {
        // Also eventually include skid buffer space.
        bool usedIQ;
        unsigned freeIQEntries;
        bool usedLSQ;
        unsigned freeLSQEntries;

        unsigned iqCount;
        unsigned ldstqCount;

        unsigned dispatched;
        unsigned dispatchedToLSQ;
    };

    iewComm iewInfo[Impl::MaxThreads];

    struct commitComm {
        bool usedROB;
        unsigned freeROBEntries;
        bool emptyROB;

        bool squash;
        bool robSquashing;

        bool branchMispredict;
        bool branchTaken;
        Addr mispredPC;
        Addr nextPC;
        Addr nextNPC;
        Addr nextMicroPC;

        // Represents the instruction that has either been retired or
        // squashed.  Similar to having a single bus that broadcasts the
        // retired or squashed sequence number.
        InstSeqNum doneSeqNum;

        //Just in case we want to do a commit/squash on a cycle
        //(necessary for multiple ROBs?)
        bool commitInsts;
        InstSeqNum squashSeqNum;

        // Communication specifically to the IQ to tell the IQ that it can
        // schedule a non-speculative instruction.
        InstSeqNum nonSpecSeqNum;

        // Hack for now to send back an uncached access to the IEW stage.
        typedef typename Impl::DynInstPtr DynInstPtr;
        bool uncached;
        DynInstPtr uncachedLoad;

        bool interruptPending;
        bool clearInterrupt;
    };

    commitComm commitInfo[Impl::MaxThreads];

    bool decodeBlock[Impl::MaxThreads];
    bool decodeUnblock[Impl::MaxThreads];
    bool renameBlock[Impl::MaxThreads];
    bool renameUnblock[Impl::MaxThreads];
    bool iewBlock[Impl::MaxThreads];
    bool iewUnblock[Impl::MaxThreads];
    bool commitBlock[Impl::MaxThreads];
    bool commitUnblock[Impl::MaxThreads];
};

#endif //__CPU_O3_COMM_HH__
