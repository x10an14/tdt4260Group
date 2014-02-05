/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 * Authors: Erik Hallnor
 *          Steve Reinhardt
 */

// FIX ME: make trackBlkAddr use blocksize from actual cache, not hard coded

#include <iomanip>
#include <set>
#include <string>
#include <vector>

#include "base/misc.hh"
#include "base/statistics.hh"
#include "cpu/memtest/memtest.hh"
#include "mem/mem_object.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/sim_events.hh"
#include "sim/stats.hh"

using namespace std;

int TESTER_ALLOCATOR=0;

bool
MemTest::CpuPort::recvTiming(PacketPtr pkt)
{
    if (pkt->isResponse()) {
        memtest->completeRequest(pkt);
    } else {
        // must be snoop upcall
        assert(pkt->isRequest());
        assert(pkt->getDest() == Packet::Broadcast);
    }
    return true;
}

Tick
MemTest::CpuPort::recvAtomic(PacketPtr pkt)
{
    // must be snoop upcall
    assert(pkt->isRequest());
    assert(pkt->getDest() == Packet::Broadcast);
    return curTick;
}

void
MemTest::CpuPort::recvFunctional(PacketPtr pkt)
{
    //Do nothing if we see one come through
//    if (curTick != 0)//Supress warning durring initialization
//        warn("Functional Writes not implemented in MemTester\n");
    //Need to find any response values that intersect and update
    return;
}

void
MemTest::CpuPort::recvStatusChange(Status status)
{
    if (status == RangeChange) {
        if (!snoopRangeSent) {
            snoopRangeSent = true;
            sendStatusChange(Port::RangeChange);
        }
        return;
    }

    panic("MemTest doesn't expect recvStatusChange callback!");
}

void
MemTest::CpuPort::recvRetry()
{
    memtest->doRetry();
}

void
MemTest::sendPkt(PacketPtr pkt) {
    if (atomic) {
        cachePort.sendAtomic(pkt);
        completeRequest(pkt);
    }
    else if (!cachePort.sendTiming(pkt)) {
        accessRetry = true;
        retryPkt = pkt;
    }

}

MemTest::MemTest(const Params *p)
    : MemObject(p),
      tickEvent(this),
      cachePort("test", this),
      funcPort("functional", this),
      retryPkt(NULL),
//      mainMem(main_mem),
//      checkMem(check_mem),
      size(p->memory_size),
      percentReads(p->percent_reads),
      percentFunctional(p->percent_functional),
      percentUncacheable(p->percent_uncacheable),
      progressInterval(p->progress_interval),
      nextProgressMessage(p->progress_interval),
      percentSourceUnaligned(p->percent_source_unaligned),
      percentDestUnaligned(p->percent_dest_unaligned),
      maxLoads(p->max_loads),
      atomic(p->atomic)
{
    vector<string> cmd;
    cmd.push_back("/bin/ls");
    vector<string> null_vec;
    //  thread = new SimpleThread(NULL, 0, NULL, 0, mainMem);
    curTick = 0;

    cachePort.snoopRangeSent = false;
    funcPort.snoopRangeSent = true;

    // Needs to be masked off once we know the block size.
    traceBlockAddr = p->trace_addr;
    baseAddr1 = 0x100000;
    baseAddr2 = 0x400000;
    uncacheAddr = 0x800000;

    // set up counters
    noResponseCycles = 0;
    numReads = 0;
    schedule(tickEvent, 0);

    id = TESTER_ALLOCATOR++;

    accessRetry = false;
}

Port *
MemTest::getPort(const std::string &if_name, int idx)
{
    if (if_name == "functional")
        return &funcPort;
    else if (if_name == "test")
        return &cachePort;
    else
        panic("No Such Port\n");
}

void
MemTest::init()
{
    // By the time init() is called, the ports should be hooked up.
    blockSize = cachePort.peerBlockSize();
    blockAddrMask = blockSize - 1;
    traceBlockAddr = blockAddr(traceBlockAddr);

    // initial memory contents for both physical memory and functional
    // memory should be 0; no need to initialize them.
}


void
MemTest::completeRequest(PacketPtr pkt)
{
    Request *req = pkt->req;

    DPRINTF(MemTest, "completing %s at address %x (blk %x)\n",
            pkt->isWrite() ? "write" : "read",
            req->getPaddr(), blockAddr(req->getPaddr()));

    MemTestSenderState *state =
        dynamic_cast<MemTestSenderState *>(pkt->senderState);

    uint8_t *data = state->data;
    uint8_t *pkt_data = pkt->getPtr<uint8_t>();

    //Remove the address from the list of outstanding
    std::set<unsigned>::iterator removeAddr =
        outstandingAddrs.find(req->getPaddr());
    assert(removeAddr != outstandingAddrs.end());
    outstandingAddrs.erase(removeAddr);

    assert(pkt->isResponse());

    if (pkt->isRead()) {
        if (memcmp(pkt_data, data, pkt->getSize()) != 0) {
            panic("%s: read of %x (blk %x) @ cycle %d "
                  "returns %x, expected %x\n", name(),
                  req->getPaddr(), blockAddr(req->getPaddr()), curTick,
                  *pkt_data, *data);
        }

        numReads++;
        numReadsStat++;

        if (numReads == (uint64_t)nextProgressMessage) {
            ccprintf(cerr, "%s: completed %d read accesses @%d\n",
                     name(), numReads, curTick);
            nextProgressMessage += progressInterval;
        }

        if (maxLoads != 0 && numReads >= maxLoads)
            exitSimLoop("maximum number of loads reached");
    } else {
        assert(pkt->isWrite());
        numWritesStat++;
    }

    noResponseCycles = 0;
    delete state;
    delete [] data;
    delete pkt->req;
    delete pkt;
}

void
MemTest::regStats()
{
    using namespace Stats;

    numReadsStat
        .name(name() + ".num_reads")
        .desc("number of read accesses completed")
        ;

    numWritesStat
        .name(name() + ".num_writes")
        .desc("number of write accesses completed")
        ;

    numCopiesStat
        .name(name() + ".num_copies")
        .desc("number of copy accesses completed")
        ;
}

void
MemTest::tick()
{
    if (!tickEvent.scheduled())
        schedule(tickEvent, curTick + ticks(1));

    if (++noResponseCycles >= 500000) {
        cerr << name() << ": deadlocked at cycle " << curTick << endl;
        fatal("");
    }

    if (accessRetry) {
        return;
    }

    //make new request
    unsigned cmd = random() % 100;
    unsigned offset = random() % size;
    unsigned base = random() % 2;
    uint64_t data = random();
    unsigned access_size = random() % 4;
    bool uncacheable = (random() % 100) < percentUncacheable;

    //If we aren't doing copies, use id as offset, and do a false sharing
    //mem tester
    //We can eliminate the lower bits of the offset, and then use the id
    //to offset within the blks
    offset = blockAddr(offset);
    offset += id;
    access_size = 0;

    Request *req = new Request();
    Request::Flags flags;
    Addr paddr;

    if (uncacheable) {
        flags.set(Request::UNCACHEABLE);
        paddr = uncacheAddr + offset;
    } else {
        paddr = ((base) ? baseAddr1 : baseAddr2) + offset;
    }
    bool probe = (random() % 100 < percentFunctional) && !uncacheable;

    paddr &= ~((1 << access_size) - 1);
    req->setPhys(paddr, 1 << access_size, flags);
    req->setThreadContext(id,0);

    uint8_t *result = new uint8_t[8];

    if (cmd < percentReads) {
        // read

        // For now we only allow one outstanding request per address
        // per tester This means we assume CPU does write forwarding
        // to reads that alias something in the cpu store buffer.
        if (outstandingAddrs.find(paddr) != outstandingAddrs.end()) {
            delete [] result;
            delete req;
            return;
        }

        outstandingAddrs.insert(paddr);

        // ***** NOTE FOR RON: I'm not sure how to access checkMem. - Kevin
        funcPort.readBlob(req->getPaddr(), result, req->getSize());

        DPRINTF(MemTest,
                "initiating read at address %x (blk %x) expecting %x\n",
                req->getPaddr(), blockAddr(req->getPaddr()), *result);

        PacketPtr pkt = new Packet(req, MemCmd::ReadReq, Packet::Broadcast);
        pkt->setSrc(0);
        pkt->dataDynamicArray(new uint8_t[req->getSize()]);
        MemTestSenderState *state = new MemTestSenderState(result);
        pkt->senderState = state;

        if (probe) {
            cachePort.sendFunctional(pkt);
            completeRequest(pkt);
        } else {
            sendPkt(pkt);
        }
    } else {
        // write

        // For now we only allow one outstanding request per addreess
        // per tester.  This means we assume CPU does write forwarding
        // to reads that alias something in the cpu store buffer.
        if (outstandingAddrs.find(paddr) != outstandingAddrs.end()) {
            delete [] result;
            delete req;
            return;
        }

        outstandingAddrs.insert(paddr);

        DPRINTF(MemTest, "initiating write at address %x (blk %x) value %x\n",
                req->getPaddr(), blockAddr(req->getPaddr()), data & 0xff);

        PacketPtr pkt = new Packet(req, MemCmd::WriteReq, Packet::Broadcast);
        pkt->setSrc(0);
        uint8_t *pkt_data = new uint8_t[req->getSize()];
        pkt->dataDynamicArray(pkt_data);
        memcpy(pkt_data, &data, req->getSize());
        MemTestSenderState *state = new MemTestSenderState(result);
        pkt->senderState = state;

        funcPort.writeBlob(req->getPaddr(), pkt_data, req->getSize());

        if (probe) {
            cachePort.sendFunctional(pkt);
            completeRequest(pkt);
        } else {
            sendPkt(pkt);
        }
    }
}

void
MemTest::doRetry()
{
    if (cachePort.sendTiming(retryPkt)) {
        accessRetry = false;
        retryPkt = NULL;
    }
}


void
MemTest::printAddr(Addr a)
{
    cachePort.printAddr(a);
}


MemTest *
MemTestParams::create()
{
    return new MemTest(this);
}
