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
 * Authors: Steve Reinhardt
 */

#ifndef __CPU_SIMPLE_ATOMIC_HH__
#define __CPU_SIMPLE_ATOMIC_HH__

#include "cpu/simple/base.hh"
#include "params/AtomicSimpleCPU.hh"

class AtomicSimpleCPU : public BaseSimpleCPU
{
  public:

    AtomicSimpleCPU(AtomicSimpleCPUParams *params);
    virtual ~AtomicSimpleCPU();

    virtual void init();

  private:

    struct TickEvent : public Event
    {
        AtomicSimpleCPU *cpu;

        TickEvent(AtomicSimpleCPU *c);
        void process();
        const char *description() const;
    };

    TickEvent tickEvent;

    const int width;
    bool locked;
    const bool simulate_data_stalls;
    const bool simulate_inst_stalls;

    // main simulation loop (one cycle)
    void tick();

    class CpuPort : public Port
    {
      public:

        CpuPort(const std::string &_name, AtomicSimpleCPU *_cpu)
            : Port(_name, _cpu), cpu(_cpu)
        { }

        bool snoopRangeSent;

      protected:

        AtomicSimpleCPU *cpu;

        virtual bool recvTiming(PacketPtr pkt);

        virtual Tick recvAtomic(PacketPtr pkt);

        virtual void recvFunctional(PacketPtr pkt);

        virtual void recvStatusChange(Status status);

        virtual void recvRetry();

        virtual void getDeviceAddressRanges(AddrRangeList &resp,
            bool &snoop)
        { resp.clear(); snoop = true; }

    };
    CpuPort icachePort;

    class DcachePort : public CpuPort
    {
      public:
        DcachePort(const std::string &_name, AtomicSimpleCPU *_cpu)
            : CpuPort(_name, _cpu)
        { }

        virtual void setPeer(Port *port);
    };
    DcachePort dcachePort;

    CpuPort physmemPort;
    bool hasPhysMemPort;
    Request ifetch_req;
    Request data_read_req;
    Request data_write_req;

    bool dcache_access;
    Tick dcache_latency;

    Range<Addr> physMemAddr;

  public:

    virtual Port *getPort(const std::string &if_name, int idx = -1);

    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);
    virtual void resume();

    void switchOut();
    void takeOverFrom(BaseCPU *oldCPU);

    virtual void activateContext(int thread_num, int delay);
    virtual void suspendContext(int thread_num);

    template <class T>
    Fault read(Addr addr, T &data, unsigned flags);

    template <class T>
    Fault write(T data, Addr addr, unsigned flags, uint64_t *res);

    /**
     * Print state of address in memory system via PrintReq (for
     * debugging).
     */
    void printAddr(Addr a);
};

#endif // __CPU_SIMPLE_ATOMIC_HH__
