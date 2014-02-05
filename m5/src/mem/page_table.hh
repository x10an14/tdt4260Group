/*
 * Copyright (c) 2003 The Regents of The University of Michigan
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

/**
 * @file
 * Declaration of a non-full system Page Table.
 */

#ifndef __MEM_PAGE_TABLE_HH__
#define __MEM_PAGE_TABLE_HH__

#include <string>

#include "arch/isa_traits.hh"
#include "arch/tlb.hh"
#include "base/hashmap.hh"
#include "base/types.hh"
#include "config/the_isa.hh"
#include "mem/request.hh"
#include "sim/faults.hh"
#include "sim/serialize.hh"

class Process;

/**
 * Page Table Declaration.
 */
class PageTable
{
  protected:
    typedef m5::hash_map<Addr, TheISA::TlbEntry> PTable;
    typedef PTable::iterator PTableItr;
    PTable pTable;

    struct cacheElement {
        Addr vaddr;
        TheISA::TlbEntry entry;
    };

    struct cacheElement pTableCache[3];

    const Addr pageSize;
    const Addr offsetMask;

    Process *process;

  public:

    PageTable(Process *_process, Addr _pageSize = TheISA::VMPageSize);

    ~PageTable();

    Addr pageAlign(Addr a)  { return (a & ~offsetMask); }
    Addr pageOffset(Addr a) { return (a &  offsetMask); }

    void allocate(Addr vaddr, int64_t size);
    void remap(Addr vaddr, int64_t size, Addr new_vaddr);
    void deallocate(Addr vaddr, int64_t size);

    /**
     * Lookup function
     * @param vaddr The virtual address.
     * @return entry The page table entry corresponding to vaddr.
     */
    bool lookup(Addr vaddr, TheISA::TlbEntry &entry);

    /**
     * Translate function
     * @param vaddr The virtual address.
     * @param paddr Physical address from translation.
     * @return True if translation exists
     */
    bool translate(Addr vaddr, Addr &paddr);

    /**
     * Simplified translate function (just check for translation)
     * @param vaddr The virtual address.
     * @return True if translation exists
     */
    bool translate(Addr vaddr) { Addr dummy; return translate(vaddr, dummy); }

    /**
     * Perform a translation on the memory request, fills in paddr
     * field of req.
     * @param req The memory request.
     */
    Fault translate(RequestPtr req);

    /**
     * Update the page table cache.
     * @param vaddr virtual address (page aligned) to check
     * @param pte page table entry to return
     */
    inline void updateCache(Addr vaddr, TheISA::TlbEntry entry)
    {
        pTableCache[2].entry = pTableCache[1].entry;
        pTableCache[2].vaddr = pTableCache[1].vaddr;
        pTableCache[1].entry = pTableCache[0].entry;
        pTableCache[1].vaddr = pTableCache[0].vaddr;
        pTableCache[0].entry = entry;
        pTableCache[0].vaddr = vaddr;
    }


    void serialize(std::ostream &os);

    void unserialize(Checkpoint *cp, const std::string &section);
};

#endif // __MEM_PAGE_TABLE_HH__
