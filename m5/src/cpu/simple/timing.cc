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

#include "arch/locked_mem.hh"
#include "arch/mmaped_ipr.hh"
#include "arch/utility.hh"
#include "base/bigint.hh"
#include "config/the_isa.hh"
#include "cpu/exetrace.hh"
#include "cpu/simple/timing.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "params/TimingSimpleCPU.hh"
#include "sim/system.hh"

using namespace std;
using namespace TheISA;

Port *
TimingSimpleCPU::getPort(const std::string &if_name, int idx)
{
    if (if_name == "dcache_port")
        return &dcachePort;
    else if (if_name == "icache_port")
        return &icachePort;
    else
        panic("No Such Port\n");
}

void
TimingSimpleCPU::init()
{
    BaseCPU::init();
#if FULL_SYSTEM
    for (int i = 0; i < threadContexts.size(); ++i) {
        ThreadContext *tc = threadContexts[i];

        // initialize CPU, including PC
        TheISA::initCPU(tc, _cpuId);
    }
#endif
}

Tick
TimingSimpleCPU::CpuPort::recvAtomic(PacketPtr pkt)
{
    panic("TimingSimpleCPU doesn't expect recvAtomic callback!");
    return curTick;
}

void
TimingSimpleCPU::CpuPort::recvFunctional(PacketPtr pkt)
{
    //No internal storage to update, jusst return
    return;
}

void
TimingSimpleCPU::CpuPort::recvStatusChange(Status status)
{
    if (status == RangeChange) {
        if (!snoopRangeSent) {
            snoopRangeSent = true;
            sendStatusChange(Port::RangeChange);
        }
        return;
    }

    panic("TimingSimpleCPU doesn't expect recvStatusChange callback!");
}


void
TimingSimpleCPU::CpuPort::TickEvent::schedule(PacketPtr _pkt, Tick t)
{
    pkt = _pkt;
    cpu->schedule(this, t);
}

TimingSimpleCPU::TimingSimpleCPU(TimingSimpleCPUParams *p)
    : BaseSimpleCPU(p), fetchTranslation(this), icachePort(this, p->clock),
    dcachePort(this, p->clock), fetchEvent(this)
{
    _status = Idle;

    icachePort.snoopRangeSent = false;
    dcachePort.snoopRangeSent = false;

    ifetch_pkt = dcache_pkt = NULL;
    drainEvent = NULL;
    previousTick = 0;
    changeState(SimObject::Running);
}


TimingSimpleCPU::~TimingSimpleCPU()
{
}

void
TimingSimpleCPU::serialize(ostream &os)
{
    SimObject::State so_state = SimObject::getState();
    SERIALIZE_ENUM(so_state);
    BaseSimpleCPU::serialize(os);
}

void
TimingSimpleCPU::unserialize(Checkpoint *cp, const string &section)
{
    SimObject::State so_state;
    UNSERIALIZE_ENUM(so_state);
    BaseSimpleCPU::unserialize(cp, section);
}

unsigned int
TimingSimpleCPU::drain(Event *drain_event)
{
    // TimingSimpleCPU is ready to drain if it's not waiting for
    // an access to complete.
    if (_status == Idle || _status == Running || _status == SwitchedOut) {
        changeState(SimObject::Drained);
        return 0;
    } else {
        changeState(SimObject::Draining);
        drainEvent = drain_event;
        return 1;
    }
}

void
TimingSimpleCPU::resume()
{
    DPRINTF(SimpleCPU, "Resume\n");
    if (_status != SwitchedOut && _status != Idle) {
        assert(system->getMemoryMode() == Enums::timing);

        if (fetchEvent.scheduled())
           deschedule(fetchEvent);

        schedule(fetchEvent, nextCycle());
    }

    changeState(SimObject::Running);
}

void
TimingSimpleCPU::switchOut()
{
    assert(_status == Running || _status == Idle);
    _status = SwitchedOut;
    numCycles += tickToCycles(curTick - previousTick);

    // If we've been scheduled to resume but are then told to switch out,
    // we'll need to cancel it.
    if (fetchEvent.scheduled())
        deschedule(fetchEvent);
}


void
TimingSimpleCPU::takeOverFrom(BaseCPU *oldCPU)
{
    BaseCPU::takeOverFrom(oldCPU, &icachePort, &dcachePort);

    // if any of this CPU's ThreadContexts are active, mark the CPU as
    // running and schedule its tick event.
    for (int i = 0; i < threadContexts.size(); ++i) {
        ThreadContext *tc = threadContexts[i];
        if (tc->status() == ThreadContext::Active && _status != Running) {
            _status = Running;
            break;
        }
    }

    if (_status != Running) {
        _status = Idle;
    }
    assert(threadContexts.size() == 1);
    previousTick = curTick;
}


void
TimingSimpleCPU::activateContext(int thread_num, int delay)
{
    DPRINTF(SimpleCPU, "ActivateContext %d (%d cycles)\n", thread_num, delay);

    assert(thread_num == 0);
    assert(thread);

    assert(_status == Idle);

    notIdleFraction++;
    _status = Running;

    // kick things off by initiating the fetch of the next instruction
    schedule(fetchEvent, nextCycle(curTick + ticks(delay)));
}


void
TimingSimpleCPU::suspendContext(int thread_num)
{
    DPRINTF(SimpleCPU, "SuspendContext %d\n", thread_num);

    assert(thread_num == 0);
    assert(thread);

    if (_status == Idle)
        return;

    assert(_status == Running);

    // just change status to Idle... if status != Running,
    // completeInst() will not initiate fetch of next instruction.

    notIdleFraction--;
    _status = Idle;
}

bool
TimingSimpleCPU::handleReadPacket(PacketPtr pkt)
{
    RequestPtr req = pkt->req;
    if (req->isMmapedIpr()) {
        Tick delay;
        delay = TheISA::handleIprRead(thread->getTC(), pkt);
        new IprEvent(pkt, this, nextCycle(curTick + delay));
        _status = DcacheWaitResponse;
        dcache_pkt = NULL;
    } else if (!dcachePort.sendTiming(pkt)) {
        _status = DcacheRetry;
        dcache_pkt = pkt;
    } else {
        _status = DcacheWaitResponse;
        // memory system takes ownership of packet
        dcache_pkt = NULL;
    }
    return dcache_pkt == NULL;
}

void
TimingSimpleCPU::sendData(RequestPtr req, uint8_t *data, uint64_t *res,
                          bool read)
{
    PacketPtr pkt;
    buildPacket(pkt, req, read);
    pkt->dataDynamic<uint8_t>(data);
    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        assert(!dcache_pkt);
        pkt->makeResponse();
        completeDataAccess(pkt);
    } else if (read) {
        handleReadPacket(pkt);
    } else {
        bool do_access = true;  // flag to suppress cache access

        if (req->isLLSC()) {
            do_access = TheISA::handleLockedWrite(thread, req);
        } else if (req->isCondSwap()) {
            assert(res);
            req->setExtraData(*res);
        }

        if (do_access) {
            dcache_pkt = pkt;
            handleWritePacket();
        } else {
            _status = DcacheWaitResponse;
            completeDataAccess(pkt);
        }
    }
}

void
TimingSimpleCPU::sendSplitData(RequestPtr req1, RequestPtr req2,
                               RequestPtr req, uint8_t *data, bool read)
{
    PacketPtr pkt1, pkt2;
    buildSplitPacket(pkt1, pkt2, req1, req2, req, data, read);
    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        assert(!dcache_pkt);
        pkt1->makeResponse();
        completeDataAccess(pkt1);
    } else if (read) {
        if (handleReadPacket(pkt1)) {
            SplitFragmentSenderState * send_state =
                dynamic_cast<SplitFragmentSenderState *>(pkt1->senderState);
            send_state->clearFromParent();
            if (handleReadPacket(pkt2)) {
                send_state = dynamic_cast<SplitFragmentSenderState *>(
                        pkt1->senderState);
                send_state->clearFromParent();
            }
        }
    } else {
        dcache_pkt = pkt1;
        if (handleWritePacket()) {
            SplitFragmentSenderState * send_state =
                dynamic_cast<SplitFragmentSenderState *>(pkt1->senderState);
            send_state->clearFromParent();
            dcache_pkt = pkt2;
            if (handleWritePacket()) {
                send_state = dynamic_cast<SplitFragmentSenderState *>(
                        pkt1->senderState);
                send_state->clearFromParent();
            }
        }
    }
}

void
TimingSimpleCPU::translationFault(Fault fault)
{
    // fault may be NoFault in cases where a fault is suppressed,
    // for instance prefetches.
    numCycles += tickToCycles(curTick - previousTick);
    previousTick = curTick;

    if (traceData) {
        // Since there was a fault, we shouldn't trace this instruction.
        delete traceData;
        traceData = NULL;
    }

    postExecute();

    if (getState() == SimObject::Draining) {
        advancePC(fault);
        completeDrain();
    } else {
        advanceInst(fault);
    }
}

void
TimingSimpleCPU::buildPacket(PacketPtr &pkt, RequestPtr req, bool read)
{
    MemCmd cmd;
    if (read) {
        cmd = MemCmd::ReadReq;
        if (req->isLLSC())
            cmd = MemCmd::LoadLockedReq;
    } else {
        cmd = MemCmd::WriteReq;
        if (req->isLLSC()) {
            cmd = MemCmd::StoreCondReq;
        } else if (req->isSwap()) {
            cmd = MemCmd::SwapReq;
        }
    }
    pkt = new Packet(req, cmd, Packet::Broadcast);
}

void
TimingSimpleCPU::buildSplitPacket(PacketPtr &pkt1, PacketPtr &pkt2,
        RequestPtr req1, RequestPtr req2, RequestPtr req,
        uint8_t *data, bool read)
{
    pkt1 = pkt2 = NULL;

    assert(!req1->isMmapedIpr() && !req2->isMmapedIpr());

    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        buildPacket(pkt1, req, read);
        return;
    }

    buildPacket(pkt1, req1, read);
    buildPacket(pkt2, req2, read);

    req->setPhys(req1->getPaddr(), req->getSize(), req1->getFlags());
    PacketPtr pkt = new Packet(req, pkt1->cmd.responseCommand(),
                               Packet::Broadcast);

    pkt->dataDynamic<uint8_t>(data);
    pkt1->dataStatic<uint8_t>(data);
    pkt2->dataStatic<uint8_t>(data + req1->getSize());

    SplitMainSenderState * main_send_state = new SplitMainSenderState;
    pkt->senderState = main_send_state;
    main_send_state->fragments[0] = pkt1;
    main_send_state->fragments[1] = pkt2;
    main_send_state->outstanding = 2;
    pkt1->senderState = new SplitFragmentSenderState(pkt, 0);
    pkt2->senderState = new SplitFragmentSenderState(pkt, 1);
}

template <class T>
Fault
TimingSimpleCPU::read(Addr addr, T &data, unsigned flags)
{
    Fault fault;
    const int asid = 0;
    const ThreadID tid = 0;
    const Addr pc = thread->readPC();
    unsigned block_size = dcachePort.peerBlockSize();
    int data_size = sizeof(T);
    BaseTLB::Mode mode = BaseTLB::Read;

    if (traceData) {
        traceData->setAddr(addr);
    }

    RequestPtr req  = new Request(asid, addr, data_size,
                                  flags, pc, _cpuId, tid);

    Addr split_addr = roundDown(addr + data_size - 1, block_size);
    assert(split_addr <= addr || split_addr - addr < block_size);

    _status = DTBWaitResponse;
    if (split_addr > addr) {
        RequestPtr req1, req2;
        assert(!req->isLLSC() && !req->isSwap());
        req->splitOnVaddr(split_addr, req1, req2);

        WholeTranslationState *state =
            new WholeTranslationState(req, req1, req2, (uint8_t *)(new T),
                                      NULL, mode);
        DataTranslation<TimingSimpleCPU> *trans1 =
            new DataTranslation<TimingSimpleCPU>(this, state, 0);
        DataTranslation<TimingSimpleCPU> *trans2 =
            new DataTranslation<TimingSimpleCPU>(this, state, 1);

        thread->dtb->translateTiming(req1, tc, trans1, mode);
        thread->dtb->translateTiming(req2, tc, trans2, mode);
    } else {
        WholeTranslationState *state =
            new WholeTranslationState(req, (uint8_t *)(new T), NULL, mode);
        DataTranslation<TimingSimpleCPU> *translation
            = new DataTranslation<TimingSimpleCPU>(this, state);
        thread->dtb->translateTiming(req, tc, translation, mode);
    }

    return NoFault;
}

#ifndef DOXYGEN_SHOULD_SKIP_THIS

template
Fault
TimingSimpleCPU::read(Addr addr, Twin64_t &data, unsigned flags);

template
Fault
TimingSimpleCPU::read(Addr addr, Twin32_t &data, unsigned flags);

template
Fault
TimingSimpleCPU::read(Addr addr, uint64_t &data, unsigned flags);

template
Fault
TimingSimpleCPU::read(Addr addr, uint32_t &data, unsigned flags);

template
Fault
TimingSimpleCPU::read(Addr addr, uint16_t &data, unsigned flags);

template
Fault
TimingSimpleCPU::read(Addr addr, uint8_t &data, unsigned flags);

#endif //DOXYGEN_SHOULD_SKIP_THIS

template<>
Fault
TimingSimpleCPU::read(Addr addr, double &data, unsigned flags)
{
    return read(addr, *(uint64_t*)&data, flags);
}

template<>
Fault
TimingSimpleCPU::read(Addr addr, float &data, unsigned flags)
{
    return read(addr, *(uint32_t*)&data, flags);
}

template<>
Fault
TimingSimpleCPU::read(Addr addr, int32_t &data, unsigned flags)
{
    return read(addr, (uint32_t&)data, flags);
}

bool
TimingSimpleCPU::handleWritePacket()
{
    RequestPtr req = dcache_pkt->req;
    if (req->isMmapedIpr()) {
        Tick delay;
        delay = TheISA::handleIprWrite(thread->getTC(), dcache_pkt);
        new IprEvent(dcache_pkt, this, nextCycle(curTick + delay));
        _status = DcacheWaitResponse;
        dcache_pkt = NULL;
    } else if (!dcachePort.sendTiming(dcache_pkt)) {
        _status = DcacheRetry;
    } else {
        _status = DcacheWaitResponse;
        // memory system takes ownership of packet
        dcache_pkt = NULL;
    }
    return dcache_pkt == NULL;
}

template <class T>
Fault
TimingSimpleCPU::write(T data, Addr addr, unsigned flags, uint64_t *res)
{
    const int asid = 0;
    const ThreadID tid = 0;
    const Addr pc = thread->readPC();
    unsigned block_size = dcachePort.peerBlockSize();
    int data_size = sizeof(T);
    BaseTLB::Mode mode = BaseTLB::Write;

    if (traceData) {
        traceData->setAddr(addr);
        traceData->setData(data);
    }

    RequestPtr req = new Request(asid, addr, data_size,
                                 flags, pc, _cpuId, tid);

    Addr split_addr = roundDown(addr + data_size - 1, block_size);
    assert(split_addr <= addr || split_addr - addr < block_size);

    T *dataP = new T;
    *dataP = TheISA::htog(data);
    _status = DTBWaitResponse;
    if (split_addr > addr) {
        RequestPtr req1, req2;
        assert(!req->isLLSC() && !req->isSwap());
        req->splitOnVaddr(split_addr, req1, req2);

        WholeTranslationState *state =
            new WholeTranslationState(req, req1, req2, (uint8_t *)dataP,
                                      res, mode);
        DataTranslation<TimingSimpleCPU> *trans1 =
            new DataTranslation<TimingSimpleCPU>(this, state, 0);
        DataTranslation<TimingSimpleCPU> *trans2 =
            new DataTranslation<TimingSimpleCPU>(this, state, 1);

        thread->dtb->translateTiming(req1, tc, trans1, mode);
        thread->dtb->translateTiming(req2, tc, trans2, mode);
    } else {
        WholeTranslationState *state =
            new WholeTranslationState(req, (uint8_t *)dataP, res, mode);
        DataTranslation<TimingSimpleCPU> *translation =
            new DataTranslation<TimingSimpleCPU>(this, state);
        thread->dtb->translateTiming(req, tc, translation, mode);
    }

    // Translation faults will be returned via finishTranslation()
    return NoFault;
}


#ifndef DOXYGEN_SHOULD_SKIP_THIS
template
Fault
TimingSimpleCPU::write(Twin32_t data, Addr addr,
                       unsigned flags, uint64_t *res);

template
Fault
TimingSimpleCPU::write(Twin64_t data, Addr addr,
                       unsigned flags, uint64_t *res);

template
Fault
TimingSimpleCPU::write(uint64_t data, Addr addr,
                       unsigned flags, uint64_t *res);

template
Fault
TimingSimpleCPU::write(uint32_t data, Addr addr,
                       unsigned flags, uint64_t *res);

template
Fault
TimingSimpleCPU::write(uint16_t data, Addr addr,
                       unsigned flags, uint64_t *res);

template
Fault
TimingSimpleCPU::write(uint8_t data, Addr addr,
                       unsigned flags, uint64_t *res);

#endif //DOXYGEN_SHOULD_SKIP_THIS

template<>
Fault
TimingSimpleCPU::write(double data, Addr addr, unsigned flags, uint64_t *res)
{
    return write(*(uint64_t*)&data, addr, flags, res);
}

template<>
Fault
TimingSimpleCPU::write(float data, Addr addr, unsigned flags, uint64_t *res)
{
    return write(*(uint32_t*)&data, addr, flags, res);
}


template<>
Fault
TimingSimpleCPU::write(int32_t data, Addr addr, unsigned flags, uint64_t *res)
{
    return write((uint32_t)data, addr, flags, res);
}


void
TimingSimpleCPU::finishTranslation(WholeTranslationState *state)
{
    _status = Running;

    if (state->getFault() != NoFault) {
        if (state->isPrefetch()) {
            state->setNoFault();
        }
        delete state->data;
        state->deleteReqs();
        translationFault(state->getFault());
    } else {
        if (!state->isSplit) {
            sendData(state->mainReq, state->data, state->res,
                     state->mode == BaseTLB::Read);
        } else {
            sendSplitData(state->sreqLow, state->sreqHigh, state->mainReq,
                          state->data, state->mode == BaseTLB::Read);
        }
    }

    delete state;
}


void
TimingSimpleCPU::fetch()
{
    DPRINTF(SimpleCPU, "Fetch\n");

    if (!curStaticInst || !curStaticInst->isDelayedCommit())
        checkForInterrupts();

    checkPcEventQueue();

    bool fromRom = isRomMicroPC(thread->readMicroPC());

    if (!fromRom && !curMacroStaticInst) {
        Request *ifetch_req = new Request();
        ifetch_req->setThreadContext(_cpuId, /* thread ID */ 0);
        setupFetchRequest(ifetch_req);
        thread->itb->translateTiming(ifetch_req, tc, &fetchTranslation,
                BaseTLB::Execute);
    } else {
        _status = IcacheWaitResponse;
        completeIfetch(NULL);

        numCycles += tickToCycles(curTick - previousTick);
        previousTick = curTick;
    }
}


void
TimingSimpleCPU::sendFetch(Fault fault, RequestPtr req, ThreadContext *tc)
{
    if (fault == NoFault) {
        ifetch_pkt = new Packet(req, MemCmd::ReadReq, Packet::Broadcast);
        ifetch_pkt->dataStatic(&inst);

        if (!icachePort.sendTiming(ifetch_pkt)) {
            // Need to wait for retry
            _status = IcacheRetry;
        } else {
            // Need to wait for cache to respond
            _status = IcacheWaitResponse;
            // ownership of packet transferred to memory system
            ifetch_pkt = NULL;
        }
    } else {
        delete req;
        // fetch fault: advance directly to next instruction (fault handler)
        advanceInst(fault);
    }

    numCycles += tickToCycles(curTick - previousTick);
    previousTick = curTick;
}


void
TimingSimpleCPU::advanceInst(Fault fault)
{
    if (fault != NoFault || !stayAtPC)
        advancePC(fault);

    if (_status == Running) {
        // kick off fetch of next instruction... callback from icache
        // response will cause that instruction to be executed,
        // keeping the CPU running.
        fetch();
    }
}


void
TimingSimpleCPU::completeIfetch(PacketPtr pkt)
{
    DPRINTF(SimpleCPU, "Complete ICache Fetch\n");

    // received a response from the icache: execute the received
    // instruction

    assert(!pkt || !pkt->isError());
    assert(_status == IcacheWaitResponse);

    _status = Running;

    numCycles += tickToCycles(curTick - previousTick);
    previousTick = curTick;

    if (getState() == SimObject::Draining) {
        if (pkt) {
            delete pkt->req;
            delete pkt;
        }

        completeDrain();
        return;
    }

    preExecute();
    if (curStaticInst &&
            curStaticInst->isMemRef() && !curStaticInst->isDataPrefetch()) {
        // load or store: just send to dcache
        Fault fault = curStaticInst->initiateAcc(this, traceData);
        if (_status != Running) {
            // instruction will complete in dcache response callback
            assert(_status == DcacheWaitResponse ||
                    _status == DcacheRetry || DTBWaitResponse);
            assert(fault == NoFault);
        } else {
            if (fault != NoFault && traceData) {
                // If there was a fault, we shouldn't trace this instruction.
                delete traceData;
                traceData = NULL;
            }

            postExecute();
            // @todo remove me after debugging with legion done
            if (curStaticInst && (!curStaticInst->isMicroop() ||
                        curStaticInst->isFirstMicroop()))
                instCnt++;
            advanceInst(fault);
        }
    } else if (curStaticInst) {
        // non-memory instruction: execute completely now
        Fault fault = curStaticInst->execute(this, traceData);

        // keep an instruction count
        if (fault == NoFault)
            countInst();
        else if (traceData) {
            // If there was a fault, we shouldn't trace this instruction.
            delete traceData;
            traceData = NULL;
        }

        postExecute();
        // @todo remove me after debugging with legion done
        if (curStaticInst && (!curStaticInst->isMicroop() ||
                    curStaticInst->isFirstMicroop()))
            instCnt++;
        advanceInst(fault);
    } else {
        advanceInst(NoFault);
    }

    if (pkt) {
        delete pkt->req;
        delete pkt;
    }
}

void
TimingSimpleCPU::IcachePort::ITickEvent::process()
{
    cpu->completeIfetch(pkt);
}

bool
TimingSimpleCPU::IcachePort::recvTiming(PacketPtr pkt)
{
    if (pkt->isResponse() && !pkt->wasNacked()) {
        // delay processing of returned data until next CPU clock edge
        Tick next_tick = cpu->nextCycle(curTick);

        if (next_tick == curTick)
            cpu->completeIfetch(pkt);
        else
            tickEvent.schedule(pkt, next_tick);

        return true;
    }
    else if (pkt->wasNacked()) {
        assert(cpu->_status == IcacheWaitResponse);
        pkt->reinitNacked();
        if (!sendTiming(pkt)) {
            cpu->_status = IcacheRetry;
            cpu->ifetch_pkt = pkt;
        }
    }
    //Snooping a Coherence Request, do nothing
    return true;
}

void
TimingSimpleCPU::IcachePort::recvRetry()
{
    // we shouldn't get a retry unless we have a packet that we're
    // waiting to transmit
    assert(cpu->ifetch_pkt != NULL);
    assert(cpu->_status == IcacheRetry);
    PacketPtr tmp = cpu->ifetch_pkt;
    if (sendTiming(tmp)) {
        cpu->_status = IcacheWaitResponse;
        cpu->ifetch_pkt = NULL;
    }
}

void
TimingSimpleCPU::completeDataAccess(PacketPtr pkt)
{
    // received a response from the dcache: complete the load or store
    // instruction
    assert(!pkt->isError());

    numCycles += tickToCycles(curTick - previousTick);
    previousTick = curTick;

    if (pkt->senderState) {
        SplitFragmentSenderState * send_state =
            dynamic_cast<SplitFragmentSenderState *>(pkt->senderState);
        assert(send_state);
        delete pkt->req;
        delete pkt;
        PacketPtr big_pkt = send_state->bigPkt;
        delete send_state;
        
        SplitMainSenderState * main_send_state =
            dynamic_cast<SplitMainSenderState *>(big_pkt->senderState);
        assert(main_send_state);
        // Record the fact that this packet is no longer outstanding.
        assert(main_send_state->outstanding != 0);
        main_send_state->outstanding--;

        if (main_send_state->outstanding) {
            return;
        } else {
            delete main_send_state;
            big_pkt->senderState = NULL;
            pkt = big_pkt;
        }
    }

    assert(_status == DcacheWaitResponse || _status == DTBWaitResponse);
    _status = Running;

    Fault fault = curStaticInst->completeAcc(pkt, this, traceData);

    // keep an instruction count
    if (fault == NoFault)
        countInst();
    else if (traceData) {
        // If there was a fault, we shouldn't trace this instruction.
        delete traceData;
        traceData = NULL;
    }

    // the locked flag may be cleared on the response packet, so check
    // pkt->req and not pkt to see if it was a load-locked
    if (pkt->isRead() && pkt->req->isLLSC()) {
        TheISA::handleLockedRead(thread, pkt->req);
    }

    delete pkt->req;
    delete pkt;

    postExecute();

    if (getState() == SimObject::Draining) {
        advancePC(fault);
        completeDrain();

        return;
    }

    advanceInst(fault);
}


void
TimingSimpleCPU::completeDrain()
{
    DPRINTF(Config, "Done draining\n");
    changeState(SimObject::Drained);
    drainEvent->process();
}

void
TimingSimpleCPU::DcachePort::setPeer(Port *port)
{
    Port::setPeer(port);

#if FULL_SYSTEM
    // Update the ThreadContext's memory ports (Functional/Virtual
    // Ports)
    cpu->tcBase()->connectMemPorts(cpu->tcBase());
#endif
}

bool
TimingSimpleCPU::DcachePort::recvTiming(PacketPtr pkt)
{
    if (pkt->isResponse() && !pkt->wasNacked()) {
        // delay processing of returned data until next CPU clock edge
        Tick next_tick = cpu->nextCycle(curTick);

        if (next_tick == curTick) {
            cpu->completeDataAccess(pkt);
        } else {
            tickEvent.schedule(pkt, next_tick);
        }

        return true;
    }
    else if (pkt->wasNacked()) {
        assert(cpu->_status == DcacheWaitResponse);
        pkt->reinitNacked();
        if (!sendTiming(pkt)) {
            cpu->_status = DcacheRetry;
            cpu->dcache_pkt = pkt;
        }
    }
    //Snooping a Coherence Request, do nothing
    return true;
}

void
TimingSimpleCPU::DcachePort::DTickEvent::process()
{
    cpu->completeDataAccess(pkt);
}

void
TimingSimpleCPU::DcachePort::recvRetry()
{
    // we shouldn't get a retry unless we have a packet that we're
    // waiting to transmit
    assert(cpu->dcache_pkt != NULL);
    assert(cpu->_status == DcacheRetry);
    PacketPtr tmp = cpu->dcache_pkt;
    if (tmp->senderState) {
        // This is a packet from a split access.
        SplitFragmentSenderState * send_state =
            dynamic_cast<SplitFragmentSenderState *>(tmp->senderState);
        assert(send_state);
        PacketPtr big_pkt = send_state->bigPkt;
        
        SplitMainSenderState * main_send_state =
            dynamic_cast<SplitMainSenderState *>(big_pkt->senderState);
        assert(main_send_state);

        if (sendTiming(tmp)) {
            // If we were able to send without retrying, record that fact
            // and try sending the other fragment.
            send_state->clearFromParent();
            int other_index = main_send_state->getPendingFragment();
            if (other_index > 0) {
                tmp = main_send_state->fragments[other_index];
                cpu->dcache_pkt = tmp;
                if ((big_pkt->isRead() && cpu->handleReadPacket(tmp)) ||
                        (big_pkt->isWrite() && cpu->handleWritePacket())) {
                    main_send_state->fragments[other_index] = NULL;
                }
            } else {
                cpu->_status = DcacheWaitResponse;
                // memory system takes ownership of packet
                cpu->dcache_pkt = NULL;
            }
        }
    } else if (sendTiming(tmp)) {
        cpu->_status = DcacheWaitResponse;
        // memory system takes ownership of packet
        cpu->dcache_pkt = NULL;
    }
}

TimingSimpleCPU::IprEvent::IprEvent(Packet *_pkt, TimingSimpleCPU *_cpu,
    Tick t)
    : pkt(_pkt), cpu(_cpu)
{
    cpu->schedule(this, t);
}

void
TimingSimpleCPU::IprEvent::process()
{
    cpu->completeDataAccess(pkt);
}

const char *
TimingSimpleCPU::IprEvent::description() const
{
    return "Timing Simple CPU Delay IPR event";
}


void
TimingSimpleCPU::printAddr(Addr a)
{
    dcachePort.printAddr(a);
}


////////////////////////////////////////////////////////////////////////
//
//  TimingSimpleCPU Simulation Object
//
TimingSimpleCPU *
TimingSimpleCPUParams::create()
{
    numThreads = 1;
#if !FULL_SYSTEM
    if (workload.size() != 1)
        panic("only one workload allowed");
#endif
    return new TimingSimpleCPU(this);
}
