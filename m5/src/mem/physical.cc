/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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
 * Authors: Ron Dreslinski
 *          Ali Saidi
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "arch/registers.hh"
#include "base/misc.hh"
#include "base/random.hh"
#include "base/types.hh"
#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "mem/packet_access.hh"
#include "mem/physical.hh"
#include "sim/eventq.hh"

using namespace std;
using namespace TheISA;

PhysicalMemory::PhysicalMemory(const Params *p)
    : MemObject(p), pmemAddr(NULL), pagePtr(0),
      lat(p->latency), lat_var(p->latency_var),
      cachedSize(params()->range.size()), cachedStart(params()->range.start)
{
    if (params()->range.size() % TheISA::PageBytes != 0)
        panic("Memory Size not divisible by page size\n");

    if (params()->null)
        return;

    int map_flags = MAP_ANON | MAP_PRIVATE;
    pmemAddr = (uint8_t *)mmap(NULL, params()->range.size(),
                               PROT_READ | PROT_WRITE, map_flags, -1, 0);

    if (pmemAddr == (void *)MAP_FAILED) {
        perror("mmap");
        fatal("Could not mmap!\n");
    }

    //If requested, initialize all the memory to 0
    if (p->zero)
        memset(pmemAddr, 0, p->range.size());
}

void
PhysicalMemory::init()
{
    if (ports.size() == 0) {
        fatal("PhysicalMemory object %s is unconnected!", name());
    }

    for (PortIterator pi = ports.begin(); pi != ports.end(); ++pi) {
        if (*pi)
            (*pi)->sendStatusChange(Port::RangeChange);
    }
}

PhysicalMemory::~PhysicalMemory()
{
    if (pmemAddr)
        munmap((char*)pmemAddr, params()->range.size());
    //Remove memPorts?
}

Addr
PhysicalMemory::new_page()
{
    Addr return_addr = pagePtr << LogVMPageSize;
    return_addr += start();

    ++pagePtr;
    return return_addr;
}

unsigned
PhysicalMemory::deviceBlockSize() const
{
    //Can accept anysize request
    return 0;
}

Tick
PhysicalMemory::calculateLatency(PacketPtr pkt)
{
    Tick latency = lat;
    if (lat_var != 0)
        latency += random_mt.random<Tick>(0, lat_var);
    return latency;
}



// Add load-locked to tracking list.  Should only be called if the
// operation is a load and the LLSC flag is set.
void
PhysicalMemory::trackLoadLocked(PacketPtr pkt)
{
    Request *req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());

    // first we check if we already have a locked addr for this
    // xc.  Since each xc only gets one, we just update the
    // existing record with the new address.
    list<LockedAddr>::iterator i;

    for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
        if (i->matchesContext(req)) {
            DPRINTF(LLSC, "Modifying lock record: context %d addr %#x\n",
                    req->contextId(), paddr);
            i->addr = paddr;
            return;
        }
    }

    // no record for this xc: need to allocate a new one
    DPRINTF(LLSC, "Adding lock record: context %d addr %#x\n",
            req->contextId(), paddr);
    lockedAddrList.push_front(LockedAddr(req));
}


// Called on *writes* only... both regular stores and
// store-conditional operations.  Check for conventional stores which
// conflict with locked addresses, and for success/failure of store
// conditionals.
bool
PhysicalMemory::checkLockedAddrList(PacketPtr pkt)
{
    Request *req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());
    bool isLLSC = pkt->isLLSC();

    // Initialize return value.  Non-conditional stores always
    // succeed.  Assume conditional stores will fail until proven
    // otherwise.
    bool success = !isLLSC;

    // Iterate over list.  Note that there could be multiple matching
    // records, as more than one context could have done a load locked
    // to this location.
    list<LockedAddr>::iterator i = lockedAddrList.begin();

    while (i != lockedAddrList.end()) {

        if (i->addr == paddr) {
            // we have a matching address

            if (isLLSC && i->matchesContext(req)) {
                // it's a store conditional, and as far as the memory
                // system can tell, the requesting context's lock is
                // still valid.
                DPRINTF(LLSC, "StCond success: context %d addr %#x\n",
                        req->contextId(), paddr);
                success = true;
            }

            // Get rid of our record of this lock and advance to next
            DPRINTF(LLSC, "Erasing lock record: context %d addr %#x\n",
                    i->contextId, paddr);
            i = lockedAddrList.erase(i);
        }
        else {
            // no match: advance to next record
            ++i;
        }
    }

    if (isLLSC) {
        req->setExtraData(success ? 1 : 0);
    }

    return success;
}


#if TRACING_ON

#define CASE(A, T)                                                      \
  case sizeof(T):                                                       \
    DPRINTF(MemoryAccess,"%s of size %i on address 0x%x data 0x%x\n",   \
            A, pkt->getSize(), pkt->getAddr(), pkt->get<T>());          \
  break


#define TRACE_PACKET(A)                                                 \
    do {                                                                \
        switch (pkt->getSize()) {                                       \
          CASE(A, uint64_t);                                            \
          CASE(A, uint32_t);                                            \
          CASE(A, uint16_t);                                            \
          CASE(A, uint8_t);                                             \
          default:                                                      \
            DPRINTF(MemoryAccess, "%s of size %i on address 0x%x\n",    \
                    A, pkt->getSize(), pkt->getAddr());                 \
        }                                                               \
    } while (0)

#else

#define TRACE_PACKET(A)

#endif

Tick
PhysicalMemory::doAtomicAccess(PacketPtr pkt)
{
    assert(pkt->getAddr() >= start() &&
           pkt->getAddr() + pkt->getSize() <= start() + size());

    if (pkt->memInhibitAsserted()) {
        DPRINTF(MemoryAccess, "mem inhibited on 0x%x: not responding\n",
                pkt->getAddr());
        return 0;
    }

    uint8_t *hostAddr = pmemAddr + pkt->getAddr() - start();

    if (pkt->cmd == MemCmd::SwapReq) {
        IntReg overwrite_val;
        bool overwrite_mem;
        uint64_t condition_val64;
        uint32_t condition_val32;

        if (!pmemAddr)
            panic("Swap only works if there is real memory (i.e. null=False)");
        assert(sizeof(IntReg) >= pkt->getSize());

        overwrite_mem = true;
        // keep a copy of our possible write value, and copy what is at the
        // memory address into the packet
        std::memcpy(&overwrite_val, pkt->getPtr<uint8_t>(), pkt->getSize());
        std::memcpy(pkt->getPtr<uint8_t>(), hostAddr, pkt->getSize());

        if (pkt->req->isCondSwap()) {
            if (pkt->getSize() == sizeof(uint64_t)) {
                condition_val64 = pkt->req->getExtraData();
                overwrite_mem = !std::memcmp(&condition_val64, hostAddr,
                                             sizeof(uint64_t));
            } else if (pkt->getSize() == sizeof(uint32_t)) {
                condition_val32 = (uint32_t)pkt->req->getExtraData();
                overwrite_mem = !std::memcmp(&condition_val32, hostAddr,
                                             sizeof(uint32_t));
            } else
                panic("Invalid size for conditional read/write\n");
        }

        if (overwrite_mem)
            std::memcpy(hostAddr, &overwrite_val, pkt->getSize());

        assert(!pkt->req->isInstFetch());
        TRACE_PACKET("Read/Write");
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        if (pkt->isLLSC()) {
            trackLoadLocked(pkt);
        }
        if (pmemAddr)
            memcpy(pkt->getPtr<uint8_t>(), hostAddr, pkt->getSize());
        TRACE_PACKET(pkt->req->isInstFetch() ? "IFetch" : "Read");
    } else if (pkt->isWrite()) {
        if (writeOK(pkt)) {
            if (pmemAddr)
                memcpy(hostAddr, pkt->getPtr<uint8_t>(), pkt->getSize());
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Write");
        }
    } else if (pkt->isInvalidate()) {
        //upgrade or invalidate
        if (pkt->needsResponse()) {
            pkt->makeAtomicResponse();
        }
    } else {
        panic("unimplemented");
    }

    if (pkt->needsResponse()) {
        pkt->makeAtomicResponse();
    }
    return calculateLatency(pkt);
}


void
PhysicalMemory::doFunctionalAccess(PacketPtr pkt)
{
    assert(pkt->getAddr() >= start() &&
           pkt->getAddr() + pkt->getSize() <= start() + size());


    uint8_t *hostAddr = pmemAddr + pkt->getAddr() - start();

    if (pkt->isRead()) {
        if (pmemAddr)
            memcpy(pkt->getPtr<uint8_t>(), hostAddr, pkt->getSize());
        TRACE_PACKET("Read");
        pkt->makeAtomicResponse();
    } else if (pkt->isWrite()) {
        if (pmemAddr)
            memcpy(hostAddr, pkt->getPtr<uint8_t>(), pkt->getSize());
        TRACE_PACKET("Write");
        pkt->makeAtomicResponse();
    } else if (pkt->isPrint()) {
        Packet::PrintReqState *prs =
            dynamic_cast<Packet::PrintReqState*>(pkt->senderState);
        // Need to call printLabels() explicitly since we're not going
        // through printObj().
        prs->printLabels();
        // Right now we just print the single byte at the specified address.
        ccprintf(prs->os, "%s%#x\n", prs->curPrefix(), *hostAddr);
    } else {
        panic("PhysicalMemory: unimplemented functional command %s",
              pkt->cmdString());
    }
}


Port *
PhysicalMemory::getPort(const std::string &if_name, int idx)
{
    // Accept request for "functional" port for backwards compatibility
    // with places where this function is called from C++.  I'd prefer
    // to move all these into Python someday.
    if (if_name == "functional") {
        return new MemoryPort(csprintf("%s-functional", name()), this);
    }

    if (if_name != "port") {
        panic("PhysicalMemory::getPort: unknown port %s requested", if_name);
    }

    if (idx >= (int)ports.size()) {
        ports.resize(idx + 1);
    }

    if (ports[idx] != NULL) {
        panic("PhysicalMemory::getPort: port %d already assigned", idx);
    }

    MemoryPort *port =
        new MemoryPort(csprintf("%s-port%d", name(), idx), this);

    ports[idx] = port;
    return port;
}


void
PhysicalMemory::recvStatusChange(Port::Status status)
{
}

PhysicalMemory::MemoryPort::MemoryPort(const std::string &_name,
                                       PhysicalMemory *_memory)
    : SimpleTimingPort(_name, _memory), memory(_memory)
{ }

void
PhysicalMemory::MemoryPort::recvStatusChange(Port::Status status)
{
    memory->recvStatusChange(status);
}

void
PhysicalMemory::MemoryPort::getDeviceAddressRanges(AddrRangeList &resp,
                                                   bool &snoop)
{
    memory->getAddressRanges(resp, snoop);
}

void
PhysicalMemory::getAddressRanges(AddrRangeList &resp, bool &snoop)
{
    snoop = false;
    resp.clear();
    resp.push_back(RangeSize(start(), params()->range.size()));
}

unsigned
PhysicalMemory::MemoryPort::deviceBlockSize() const
{
    return memory->deviceBlockSize();
}

Tick
PhysicalMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return memory->doAtomicAccess(pkt);
}

void
PhysicalMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(memory->name());

    if (!checkFunctional(pkt)) {
        // Default implementation of SimpleTimingPort::recvFunctional()
        // calls recvAtomic() and throws away the latency; we can save a
        // little here by just not calculating the latency.
        memory->doFunctionalAccess(pkt);
    }

    pkt->popLabel();
}

unsigned int
PhysicalMemory::drain(Event *de)
{
    int count = 0;
    for (PortIterator pi = ports.begin(); pi != ports.end(); ++pi) {
        count += (*pi)->drain(de);
    }

    if (count)
        changeState(Draining);
    else
        changeState(Drained);
    return count;
}

void
PhysicalMemory::serialize(ostream &os)
{
    if (!pmemAddr)
        return;

    gzFile compressedMem;
    string filename = name() + ".physmem";

    SERIALIZE_SCALAR(filename);

    // write memory file
    string thefile = Checkpoint::dir() + "/" + filename.c_str();
    int fd = creat(thefile.c_str(), 0664);
    if (fd < 0) {
        perror("creat");
        fatal("Can't open physical memory checkpoint file '%s'\n", filename);
    }

    compressedMem = gzdopen(fd, "wb");
    if (compressedMem == NULL)
        fatal("Insufficient memory to allocate compression state for %s\n",
                filename);

    if (gzwrite(compressedMem, pmemAddr, params()->range.size()) !=
        (int)params()->range.size()) {
        fatal("Write failed on physical memory checkpoint file '%s'\n",
              filename);
    }

    if (gzclose(compressedMem))
        fatal("Close failed on physical memory checkpoint file '%s'\n",
              filename);
}

void
PhysicalMemory::unserialize(Checkpoint *cp, const string &section)
{
    if (!pmemAddr)
        return;

    gzFile compressedMem;
    long *tempPage;
    long *pmem_current;
    uint64_t curSize;
    uint32_t bytesRead;
    const uint32_t chunkSize = 16384;

    string filename;

    UNSERIALIZE_SCALAR(filename);

    filename = cp->cptDir + "/" + filename;

    // mmap memoryfile
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("open");
        fatal("Can't open physical memory checkpoint file '%s'", filename);
    }

    compressedMem = gzdopen(fd, "rb");
    if (compressedMem == NULL)
        fatal("Insufficient memory to allocate compression state for %s\n",
                filename);

    // unmap file that was mmaped in the constructor
    // This is done here to make sure that gzip and open don't muck with our
    // nice large space of memory before we reallocate it
    munmap((char*)pmemAddr, params()->range.size());

    pmemAddr = (uint8_t *)mmap(NULL, params()->range.size(),
        PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (pmemAddr == (void *)MAP_FAILED) {
        perror("mmap");
        fatal("Could not mmap physical memory!\n");
    }

    curSize = 0;
    tempPage = (long*)malloc(chunkSize);
    if (tempPage == NULL)
        fatal("Unable to malloc memory to read file %s\n", filename);

    /* Only copy bytes that are non-zero, so we don't give the VM system hell */
    while (curSize < params()->range.size()) {
        bytesRead = gzread(compressedMem, tempPage, chunkSize);
        if (bytesRead == 0)
            break;

        assert(bytesRead % sizeof(long) == 0);

        for (uint32_t x = 0; x < bytesRead / sizeof(long); x++)
        {
             if (*(tempPage+x) != 0) {
                 pmem_current = (long*)(pmemAddr + curSize + x * sizeof(long));
                 *pmem_current = *(tempPage+x);
             }
        }
        curSize += bytesRead;
    }

    free(tempPage);

    if (gzclose(compressedMem))
        fatal("Close failed on physical memory checkpoint file '%s'\n",
              filename);

}

PhysicalMemory *
PhysicalMemoryParams::create()
{
    return new PhysicalMemory(this);
}
