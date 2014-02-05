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
 */

/* @file
 */

#ifndef __PHYSICAL_MEMORY_HH__
#define __PHYSICAL_MEMORY_HH__

#include <map>
#include <string>

#include "base/range.hh"
#include "mem/mem_object.hh"
#include "mem/packet.hh"
#include "mem/tport.hh"
#include "params/PhysicalMemory.hh"
#include "sim/eventq.hh"

//
// Functional model for a contiguous block of physical memory. (i.e. RAM)
//
class PhysicalMemory : public MemObject
{
  protected:

    class MemoryPort : public SimpleTimingPort
    {
        PhysicalMemory *memory;

      public:

        MemoryPort(const std::string &_name, PhysicalMemory *_memory);

      protected:

        virtual Tick recvAtomic(PacketPtr pkt);

        virtual void recvFunctional(PacketPtr pkt);

        virtual void recvStatusChange(Status status);

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
                                            bool &snoop);

        virtual unsigned deviceBlockSize() const;
    };

    int numPorts;


  private:
    // prevent copying of a MainMemory object
    PhysicalMemory(const PhysicalMemory &specmem);
    const PhysicalMemory &operator=(const PhysicalMemory &specmem);

  protected:

    class LockedAddr {
      public:
        // on alpha, minimum LL/SC granularity is 16 bytes, so lower
        // bits need to masked off.
        static const Addr Addr_Mask = 0xf;

        static Addr mask(Addr paddr) { return (paddr & ~Addr_Mask); }

        Addr addr;      // locked address
        int contextId;     // locking hw context

        // check for matching execution context
        bool matchesContext(Request *req)
        {
            return (contextId == req->contextId());
        }

        LockedAddr(Request *req)
            : addr(mask(req->getPaddr())),
              contextId(req->contextId())
        {
        }
    };

    std::list<LockedAddr> lockedAddrList;

    // helper function for checkLockedAddrs(): we really want to
    // inline a quick check for an empty locked addr list (hopefully
    // the common case), and do the full list search (if necessary) in
    // this out-of-line function
    bool checkLockedAddrList(PacketPtr pkt);

    // Record the address of a load-locked operation so that we can
    // clear the execution context's lock flag if a matching store is
    // performed
    void trackLoadLocked(PacketPtr pkt);

    // Compare a store address with any locked addresses so we can
    // clear the lock flag appropriately.  Return value set to 'false'
    // if store operation should be suppressed (because it was a
    // conditional store and the address was no longer locked by the
    // requesting execution context), 'true' otherwise.  Note that
    // this method must be called on *all* stores since even
    // non-conditional stores must clear any matching lock addresses.
    bool writeOK(PacketPtr pkt) {
        Request *req = pkt->req;
        if (lockedAddrList.empty()) {
            // no locked addrs: nothing to check, store_conditional fails
            bool isLLSC = pkt->isLLSC();
            if (isLLSC) {
                req->setExtraData(0);
            }
            return !isLLSC; // only do write if not an sc
        } else {
            // iterate over list...
            return checkLockedAddrList(pkt);
        }
    }

    uint8_t *pmemAddr;
    int pagePtr;
    Tick lat;
    Tick lat_var;
    std::vector<MemoryPort*> ports;
    typedef std::vector<MemoryPort*>::iterator PortIterator;

    uint64_t cachedSize;
    uint64_t cachedStart;
  public:
    Addr new_page();
    uint64_t size() { return cachedSize; }
    uint64_t start() { return cachedStart; }

  public:
    typedef PhysicalMemoryParams Params;
    PhysicalMemory(const Params *p);
    virtual ~PhysicalMemory();

    const Params *
    params() const
    {
        return dynamic_cast<const Params *>(_params);
    }

  public:
    unsigned deviceBlockSize() const;
    void getAddressRanges(AddrRangeList &resp, bool &snoop);
    virtual Port *getPort(const std::string &if_name, int idx = -1);
    void virtual init();
    unsigned int drain(Event *de);

  protected:
    Tick doAtomicAccess(PacketPtr pkt);
    void doFunctionalAccess(PacketPtr pkt);
    virtual Tick calculateLatency(PacketPtr pkt);
    void recvStatusChange(Port::Status status);

  public:
    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);

};

#endif //__PHYSICAL_MEMORY_HH__
