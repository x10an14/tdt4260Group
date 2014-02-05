/*
 * Copyright (c) 2009 Advanced Micro Devices, Inc.
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
 */

#include "cpu/rubytest/RubyTester.hh"
#include "mem/physical.hh"
#include "mem/ruby/slicc_interface/AbstractController.hh"
#include "mem/ruby/system/RubyPort.hh"

RubyPort::RubyPort(const Params *p)
    : MemObject(p)
{
    m_version = p->version;
    assert(m_version != -1);

    physmem = p->physmem;

    m_controller = NULL;
    m_mandatory_q_ptr = NULL;

    m_request_cnt = 0;
    pio_port = NULL;
    physMemPort = NULL;
}

void
RubyPort::init()
{
    assert(m_controller != NULL);
    m_mandatory_q_ptr = m_controller->getMandatoryQueue();
}

Port *
RubyPort::getPort(const std::string &if_name, int idx)
{
    if (if_name == "port") {
        return new M5Port(csprintf("%s-port%d", name(), idx), this);
    }

    if (if_name == "pio_port") {
        // ensure there is only one pio port
        assert(pio_port == NULL);

        pio_port = new PioPort(csprintf("%s-pio-port%d", name(), idx), this);

        return pio_port;
    }

    if (if_name == "physMemPort") {
        // RubyPort should only have one port to physical memory
        assert (physMemPort == NULL);

        physMemPort = new M5Port(csprintf("%s-physMemPort", name()), this);

        return physMemPort;
    }

    if (if_name == "functional") {
        // Calls for the functional port only want to access
        // functional memory.  Therefore, directly pass these calls
        // ports to physmem.
        assert(physmem != NULL);
        return physmem->getPort(if_name, idx);
    }

    return NULL;
}

RubyPort::PioPort::PioPort(const std::string &_name,
                           RubyPort *_port)
    : SimpleTimingPort(_name, _port)
{
    DPRINTF(Ruby, "creating port to ruby sequencer to cpu %s\n", _name);
    ruby_port = _port;
}

RubyPort::M5Port::M5Port(const std::string &_name,
                         RubyPort *_port)
    : SimpleTimingPort(_name, _port)
{
    DPRINTF(Ruby, "creating port from ruby sequcner to cpu %s\n", _name);
    ruby_port = _port;
}

Tick
RubyPort::PioPort::recvAtomic(PacketPtr pkt)
{
    panic("RubyPort::PioPort::recvAtomic() not implemented!\n");
    return 0;
}

Tick
RubyPort::M5Port::recvAtomic(PacketPtr pkt)
{
    panic("RubyPort::M5Port::recvAtomic() not implemented!\n");
    return 0;
}


bool
RubyPort::PioPort::recvTiming(PacketPtr pkt)
{
    // In FS mode, ruby memory will receive pio responses from devices
    // and it must forward these responses back to the particular CPU.
    DPRINTF(MemoryAccess,  "Pio response for address %#x\n", pkt->getAddr());

    assert(pkt->isResponse());

    // First we must retrieve the request port from the sender State
    RubyPort::SenderState *senderState =
      safe_cast<RubyPort::SenderState *>(pkt->senderState);
    M5Port *port = senderState->port;
    assert(port != NULL);

    // pop the sender state from the packet
    pkt->senderState = senderState->saved;
    delete senderState;

    port->sendTiming(pkt);

    return true;
}

bool
RubyPort::M5Port::recvTiming(PacketPtr pkt)
{
    DPRINTF(MemoryAccess,
            "Timing access caught for address %#x\n", pkt->getAddr());

    //dsm: based on SimpleTimingPort::recvTiming(pkt);

    // The received packets should only be M5 requests, which should never
    // get nacked.  There used to be code to hanldle nacks here, but
    // I'm pretty sure it didn't work correctly with the drain code,
    // so that would need to be fixed if we ever added it back.
    assert(pkt->isRequest());

    if (pkt->memInhibitAsserted()) {
        warn("memInhibitAsserted???");
        // snooper will supply based on copy of packet
        // still target's responsibility to delete packet
        delete pkt;
        return true;
    }

    // Save the port in the sender state object to be used later to
    // route the response
    pkt->senderState = new SenderState(this, pkt->senderState);

    // Check for pio requests and directly send them to the dedicated
    // pio port.
    if (!isPhysMemAddress(pkt->getAddr())) {
        assert(ruby_port->pio_port != NULL);
        DPRINTF(MemoryAccess,
                "Request for address 0x%#x is assumed to be a pio request\n",
                pkt->getAddr());

        return ruby_port->pio_port->sendTiming(pkt);
    }

    // For DMA and CPU requests, translate them to ruby requests before
    // sending them to our assigned ruby port.
    RubyRequestType type = RubyRequestType_NULL;

    // If valid, copy the pc to the ruby request
    Addr pc = 0;
    if (pkt->req->hasPC()) {
        pc = pkt->req->getPC();
    }

    if (pkt->isLLSC()) {
        if (pkt->isWrite()) {
            DPRINTF(MemoryAccess, "Issuing SC\n");
            type = RubyRequestType_Locked_Write;
        } else {
            DPRINTF(MemoryAccess, "Issuing LL\n");
            assert(pkt->isRead());
            type = RubyRequestType_Locked_Read;
        }
    } else {
        if (pkt->isRead()) {
            if (pkt->req->isInstFetch()) {
                type = RubyRequestType_IFETCH;
            } else {
                type = RubyRequestType_LD;
            }
        } else if (pkt->isWrite()) {
            type = RubyRequestType_ST;
        } else if (pkt->isReadWrite()) {
            // Fix me.  This conditional will never be executed
            // because isReadWrite() is just an OR of isRead() and
            // isWrite().  Furthermore, just because the packet is a
            // read/write request does not necessary mean it is a
            // read-modify-write atomic operation.
            type = RubyRequestType_RMW_Write;
        } else {
            panic("Unsupported ruby packet type\n");
        }
    }

    RubyRequest ruby_request(pkt->getAddr(), pkt->getPtr<uint8_t>(),
                             pkt->getSize(), pc, type,
                             RubyAccessMode_Supervisor, pkt);

    // Submit the ruby request
    RequestStatus requestStatus = ruby_port->makeRequest(ruby_request);

    // If the request successfully issued or the SC request completed because
    // exclusive permission was lost, then we should return true.
    // Otherwise, we need to delete the senderStatus we just created and return
    // false.
    if ((requestStatus == RequestStatus_Issued) ||
        (requestStatus == RequestStatus_LlscFailed)) {

        // The communicate to M5 whether the SC command succeeded by seting the
        // packet's extra data.
        if (pkt->isLLSC() && pkt->isWrite()) {
            if (requestStatus == RequestStatus_LlscFailed) {
                DPRINTF(MemoryAccess, "SC failed and request completed\n");
                pkt->req->setExtraData(0);
            } else {
                pkt->req->setExtraData(1);
            }
        }
        return true;
    }

    DPRINTF(MemoryAccess,
            "Request for address #x did not issue because %s\n",
            pkt->getAddr(), RequestStatus_to_string(requestStatus));

    SenderState* senderState = safe_cast<SenderState*>(pkt->senderState);
    pkt->senderState = senderState->saved;
    delete senderState;
    return false;
}

void
RubyPort::ruby_hit_callback(PacketPtr pkt)
{
    // Retrieve the request port from the sender State
    RubyPort::SenderState *senderState =
        safe_cast<RubyPort::SenderState *>(pkt->senderState);
    M5Port *port = senderState->port;
    assert(port != NULL);

    // pop the sender state from the packet
    pkt->senderState = senderState->saved;
    delete senderState;

    port->hitCallback(pkt);
}

void
RubyPort::M5Port::hitCallback(PacketPtr pkt)
{
    bool needsResponse = pkt->needsResponse();

    DPRINTF(MemoryAccess, "Hit callback needs response %d\n", needsResponse);

    ruby_port->physMemPort->sendAtomic(pkt);

    // turn packet around to go back to requester if response expected
    if (needsResponse) {
        // sendAtomic() should already have turned packet into
        // atomic response
        assert(pkt->isResponse());
        DPRINTF(MemoryAccess, "Sending packet back over port\n");
        sendTiming(pkt);
    } else {
        delete pkt;
    }
    DPRINTF(MemoryAccess, "Hit callback done!\n");
}

bool
RubyPort::M5Port::sendTiming(PacketPtr pkt)
{
    schedSendTiming(pkt, curTick + 1); //minimum latency, must be > 0
    return true;
}

bool
RubyPort::PioPort::sendTiming(PacketPtr pkt)
{
    schedSendTiming(pkt, curTick + 1); //minimum latency, must be > 0
    return true;
}

bool
RubyPort::M5Port::isPhysMemAddress(Addr addr)
{
    AddrRangeList physMemAddrList;
    bool snoop = false;
    ruby_port->physMemPort->getPeerAddressRanges(physMemAddrList, snoop);
    for (AddrRangeIter iter = physMemAddrList.begin();
         iter != physMemAddrList.end();
         iter++) {
        if (addr >= iter->start && addr <= iter->end) {
            DPRINTF(MemoryAccess, "Request found in %#llx - %#llx range\n",
                    iter->start, iter->end);
            return true;
        }
    }
    return false;
}
