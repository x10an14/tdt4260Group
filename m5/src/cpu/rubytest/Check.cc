/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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

#include "cpu/rubytest/Check.hh"
#include "mem/ruby/common/SubBlock.hh"
#include "mem/ruby/system/Sequencer.hh"
#include "mem/ruby/system/System.hh"

typedef RubyTester::SenderState SenderState;

Check::Check(const Address& address, const Address& pc,
             int _num_cpu_sequencers, RubyTester* _tester)
    : m_num_cpu_sequencers(_num_cpu_sequencers), m_tester_ptr(_tester)
{
    m_status = TesterStatus_Idle;

    pickValue();
    pickInitiatingNode();
    changeAddress(address);
    m_pc = pc;
    m_access_mode = AccessModeType(random() % AccessModeType_NUM);
    m_store_count = 0;
}

void
Check::initiate()
{
    DPRINTF(RubyTest, "initiating\n");
    debugPrint();

    // currently no protocols support prefetches
    if (false && (random() & 0xf) == 0) {
        initiatePrefetch(); // Prefetch from random processor
    }

    if (m_status == TesterStatus_Idle) {
        initiateAction();
    } else if (m_status == TesterStatus_Ready) {
        initiateCheck();
    } else {
        // Pending - do nothing
        DPRINTF(RubyTest,
                "initiating action/check - failed: action/check is pending\n");
    }
}

void
Check::initiatePrefetch()
{
    DPRINTF(RubyTest, "initiating prefetch\n");

    int index = random() % m_num_cpu_sequencers;
    RubyTester::CpuPort* port =
        safe_cast<RubyTester::CpuPort*>(m_tester_ptr->getCpuPort(index));

    Request::Flags flags;
    flags.set(Request::PREFETCH);

    // Prefetches are assumed to be 0 sized
    Request *req = new Request(m_address.getAddress(), 0, flags, curTick,
                               m_pc.getAddress());

    Packet::Command cmd;

    // 1 in 8 chance this will be an exclusive prefetch
    if ((random() & 0x7) != 0) {
        cmd = MemCmd::ReadReq;

        // 50% chance that the request will be an instruction fetch
        if ((random() & 0x1) == 0) {
            flags.set(Request::INST_FETCH);
        }
    } else {
        cmd = MemCmd::WriteReq;
        flags.set(Request::PF_EXCLUSIVE);
    }

    PacketPtr pkt = new Packet(req, cmd, port->idx);

    // push the subblock onto the sender state.  The sequencer will
    // update the subblock on the return
    pkt->senderState =
        new SenderState(m_address, req->getSize(), pkt->senderState);

    if (port->sendTiming(pkt)) {
        DPRINTF(RubyTest, "successfully initiated prefetch.\n");
    } else {
        // If the packet did not issue, must delete
        SenderState* senderState =  safe_cast<SenderState*>(pkt->senderState);
        pkt->senderState = senderState->saved;
        delete senderState;
        delete pkt->req;
        delete pkt;

        DPRINTF(RubyTest,
                "prefetch initiation failed because Port was busy.\n");
    }
}

void
Check::initiateAction()
{
    DPRINTF(RubyTest, "initiating Action\n");
    assert(m_status == TesterStatus_Idle);

    int index = random() % m_num_cpu_sequencers;
    RubyTester::CpuPort* port =
        safe_cast<RubyTester::CpuPort*>(m_tester_ptr->getCpuPort(index));

    Request::Flags flags;

    // Create the particular address for the next byte to be written
    Address writeAddr(m_address.getAddress() + m_store_count);

    // Stores are assumed to be 1 byte-sized
    Request *req = new Request(writeAddr.getAddress(), 1, flags, curTick,
                               m_pc.getAddress());

    Packet::Command cmd;

    // 1 out of 8 chance, issue an atomic rather than a write
    // if ((random() & 0x7) == 0) {
    //     cmd = MemCmd::SwapReq;
    // } else {
    cmd = MemCmd::WriteReq;
    // }

    PacketPtr pkt = new Packet(req, cmd, port->idx);
    uint8_t* writeData = new uint8_t;
    *writeData = m_value + m_store_count;
    pkt->dataDynamic(writeData);

    DPRINTF(RubyTest, "data 0x%x check 0x%x\n",
            *(pkt->getPtr<uint8_t>()), *writeData);

    // push the subblock onto the sender state.  The sequencer will
    // update the subblock on the return
    pkt->senderState =
        new SenderState(writeAddr, req->getSize(), pkt->senderState);

    if (port->sendTiming(pkt)) {
        DPRINTF(RubyTest, "initiating action - successful\n");
        DPRINTF(RubyTest, "status before action update: %s\n",
                (TesterStatus_to_string(m_status)).c_str());
        m_status = TesterStatus_Action_Pending;
    } else {
        // If the packet did not issue, must delete
        // Note: No need to delete the data, the packet destructor
        // will delete it
        SenderState* senderState = safe_cast<SenderState*>(pkt->senderState);
        pkt->senderState = senderState->saved;
        delete senderState;
        delete pkt->req;
        delete pkt;

        DPRINTF(RubyTest, "failed to initiate action - sequencer not ready\n");
    }

    DPRINTF(RubyTest, "status after action update: %s\n",
            (TesterStatus_to_string(m_status)).c_str());
}

void
Check::initiateCheck()
{
    DPRINTF(RubyTest, "Initiating Check\n");
    assert(m_status == TesterStatus_Ready);

    int index = random() % m_num_cpu_sequencers;
    RubyTester::CpuPort* port =
        safe_cast<RubyTester::CpuPort*>(m_tester_ptr->getCpuPort(index));

    Request::Flags flags;

    // Checks are sized depending on the number of bytes written
    Request *req = new Request(m_address.getAddress(), CHECK_SIZE, flags,
                               curTick, m_pc.getAddress());

    // 50% chance that the request will be an instruction fetch
    if ((random() & 0x1) == 0) {
        flags.set(Request::INST_FETCH);
    }

    PacketPtr pkt = new Packet(req, MemCmd::ReadReq, port->idx);
    uint8_t* dataArray = new uint8_t[CHECK_SIZE];
    pkt->dataDynamicArray(dataArray);

    // push the subblock onto the sender state.  The sequencer will
    // update the subblock on the return
    pkt->senderState =
        new SenderState(m_address, req->getSize(), pkt->senderState);

    if (port->sendTiming(pkt)) {
        DPRINTF(RubyTest, "initiating check - successful\n");
        DPRINTF(RubyTest, "status before check update: %s\n",
                TesterStatus_to_string(m_status).c_str());
        m_status = TesterStatus_Check_Pending;
    } else {
        // If the packet did not issue, must delete
        // Note: No need to delete the data, the packet destructor
        // will delete it
        SenderState* senderState = safe_cast<SenderState*>(pkt->senderState);
        pkt->senderState = senderState->saved;
        delete senderState;
        delete pkt->req;
        delete pkt;

        DPRINTF(RubyTest, "failed to initiate check - cpu port not ready\n");
    }

    DPRINTF(RubyTest, "status after check update: %s\n",
            TesterStatus_to_string(m_status).c_str());
}

void
Check::performCallback(NodeID proc, SubBlock* data)
{
    Address address = data->getAddress();

    // This isn't exactly right since we now have multi-byte checks
    //  assert(getAddress() == address);

    assert(getAddress().getLineAddress() == address.getLineAddress());
    assert(data != NULL);

    DPRINTF(RubyTest, "RubyTester Callback\n");
    debugPrint();

    if (m_status == TesterStatus_Action_Pending) {
        DPRINTF(RubyTest, "Action callback write value: %d, currently %d\n",
                (m_value + m_store_count), data->getByte(0));
        // Perform store one byte at a time
        data->setByte(0, (m_value + m_store_count));
        m_store_count++;
        if (m_store_count == CHECK_SIZE) {
            m_status = TesterStatus_Ready;
        } else {
            m_status = TesterStatus_Idle;
        }
        DPRINTF(RubyTest, "Action callback return data now %d\n",
                data->getByte(0));
    } else if (m_status == TesterStatus_Check_Pending) {
        DPRINTF(RubyTest, "Check callback\n");
        // Perform load/check
        for (int byte_number=0; byte_number<CHECK_SIZE; byte_number++) {
            if (uint8(m_value + byte_number) != data->getByte(byte_number)) {
                WARN_EXPR(proc);
                WARN_EXPR(address);
                WARN_EXPR(data);
                WARN_EXPR(byte_number);
                WARN_EXPR((int)m_value + byte_number);
                WARN_EXPR((int)data->getByte(byte_number));
                WARN_EXPR(*this);
                WARN_EXPR(g_eventQueue_ptr->getTime());
                ERROR_MSG("Action/check failure");
            }
        }
        DPRINTF(RubyTest, "Action/check success\n");
        debugPrint();

        // successful check complete, increment complete
        m_tester_ptr->incrementCheckCompletions();

        m_status = TesterStatus_Idle;
        pickValue();

    } else {
        WARN_EXPR(*this);
        WARN_EXPR(proc);
        WARN_EXPR(data);
        WARN_EXPR(m_status);
        WARN_EXPR(g_eventQueue_ptr->getTime());
        ERROR_MSG("Unexpected TesterStatus");
    }

    DPRINTF(RubyTest, "proc: %d, Address: 0x%x\n", proc,
            getAddress().getLineAddress());
    DPRINTF(RubyTest, "Callback done\n");
    debugPrint();
}

void
Check::changeAddress(const Address& address)
{
    assert(m_status == TesterStatus_Idle || m_status == TesterStatus_Ready);
    m_status = TesterStatus_Idle;
    m_address = address;
    m_store_count = 0;
}

void
Check::pickValue()
{
    assert(m_status == TesterStatus_Idle);
    m_status = TesterStatus_Idle;
    m_value = random() & 0xff; // One byte
    m_store_count = 0;
}

void
Check::pickInitiatingNode()
{
    assert(m_status == TesterStatus_Idle || m_status == TesterStatus_Ready);
    m_status = TesterStatus_Idle;
    m_initiatingNode = (random() % m_num_cpu_sequencers);
    DPRINTF(RubyTest, "picked initiating node %d\n", m_initiatingNode);
    m_store_count = 0;
}

void
Check::print(std::ostream& out) const
{
    out << "["
        << m_address << ", value: "
        << (int)m_value << ", status: "
        << m_status << ", initiating node: "
        << m_initiatingNode << ", store_count: "
        << m_store_count
        << "]" << std::flush;
}

void
Check::debugPrint()
{
    DPRINTF(RubyTest,
        "[%#x, value: %d, status: %s, initiating node: %d, store_count: %d]\n",
        m_address.getAddress(), (int)m_value,
        TesterStatus_to_string(m_status).c_str(),
        m_initiatingNode, m_store_count);
}
