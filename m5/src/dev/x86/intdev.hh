/*
 * Copyright (c) 2008 The Regents of The University of Michigan
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
 * Authors: Gabe Black
 */

#ifndef __DEV_X86_INTDEV_HH__
#define __DEV_X86_INTDEV_HH__

#include <cassert>
#include <string>

#include "arch/x86/x86_traits.hh"
#include "arch/x86/intmessage.hh"
#include "mem/mem_object.hh"
#include "mem/mport.hh"
#include "sim/sim_object.hh"
#include "params/X86IntSourcePin.hh"
#include "params/X86IntSinkPin.hh"
#include "params/X86IntLine.hh"

#include <list>

namespace X86ISA {

typedef std::list<int> ApicList;

class IntDev
{
  protected:
    class IntPort : public MessagePort
    {
        IntDev * device;
        Tick latency;
        Addr intAddr;
      public:
        IntPort(const std::string &_name, MemObject * _parent,
                IntDev *dev, Tick _latency) :
            MessagePort(_name, _parent), device(dev), latency(_latency)
        {
        }

        void getDeviceAddressRanges(AddrRangeList &resp, bool &snoop)
        {
            snoop = false;
            device->getIntAddrRange(resp);
        }

        Tick recvMessage(PacketPtr pkt)
        {
            return device->recvMessage(pkt);
        }

        Tick recvResponse(PacketPtr pkt)
        {
            return device->recvResponse(pkt);
        }

        // This is x86 focused, so if this class becomes generic, this would
        // need to be moved into a subclass.
        void sendMessage(ApicList apics,
                TriggerIntMessage message, bool timing);

        void recvStatusChange(Status status)
        {
            if (status == RangeChange) {
                sendStatusChange(Port::RangeChange);
            }
        }

    };

    IntPort * intPort;

  public:
    IntDev(MemObject * parent, Tick latency = 0)
    {
        if (parent != NULL) {
            intPort = new IntPort(parent->name() + ".int_port",
                    parent, this, latency);
        } else {
            intPort = NULL;
        }
    }

    virtual ~IntDev()
    {}

    virtual void
    signalInterrupt(int line)
    {
        panic("signalInterrupt not implemented.\n");
    }

    virtual void
    raiseInterruptPin(int number)
    {
        panic("raiseInterruptPin not implemented.\n");
    }

    virtual void
    lowerInterruptPin(int number)
    {
        panic("lowerInterruptPin not implemented.\n");
    }

    virtual Tick
    recvMessage(PacketPtr pkt)
    {
        panic("recvMessage not implemented.\n");
        return 0;
    }

    virtual Tick
    recvResponse(PacketPtr pkt)
    {
        delete pkt->req;
        delete pkt;
        return 0;
    }

    virtual void
    getIntAddrRange(AddrRangeList &range_list)
    {
        panic("intAddrRange not implemented.\n");
    }
};

class IntSinkPin : public SimObject
{
  public:
    IntDev * device;
    int number;

    typedef X86IntSinkPinParams Params;

    const Params *
    params() const
    {
        return dynamic_cast<const Params *>(_params);
    }

    IntSinkPin(Params *p) : SimObject(p),
            device(dynamic_cast<IntDev *>(p->device)), number(p->number)
    {
        assert(device);
    }
};

class IntSourcePin : public SimObject
{
  protected:
    std::vector<IntSinkPin *> sinks;

  public:
    typedef X86IntSourcePinParams Params;

    const Params *
    params() const
    {
        return dynamic_cast<const Params *>(_params);
    }

    void
    addSink(IntSinkPin *sink)
    {
        sinks.push_back(sink);
    }

    void
    raise()
    {
        for (int i = 0; i < sinks.size(); i++) {
            const IntSinkPin &pin = *sinks[i];
            pin.device->raiseInterruptPin(pin.number);
        }
    }
    
    void
    lower()
    {
        for (int i = 0; i < sinks.size(); i++) {
            const IntSinkPin &pin = *sinks[i];
            pin.device->lowerInterruptPin(pin.number);
        }
    }

    IntSourcePin(Params *p) : SimObject(p)
    {}
};

class IntLine : public SimObject
{
  protected:
    IntSourcePin *source;
    IntSinkPin *sink;

  public:
    typedef X86IntLineParams Params;

    const Params *
    params() const
    {
        return dynamic_cast<const Params *>(_params);
    }

    IntLine(Params *p) : SimObject(p), source(p->source), sink(p->sink)
    {
        source->addSink(sink);
    }
};

}; // namespace X86ISA

#endif //__DEV_X86_INTDEV_HH__
