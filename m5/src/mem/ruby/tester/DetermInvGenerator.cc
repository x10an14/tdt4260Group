
/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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

/*
 * $Id$
 *
 */

// This Deterministic Generator generates GETS request for all nodes in the system
// then Invalidates them with a GETX.  The GETS and GETX request are generated one
// at a time in round-robin fashion 0...1...2...etc.

#include "mem/ruby/common/Global.hh"
#include "mem/ruby/tester/DetermInvGenerator.hh"
#include "mem/protocol/DetermInvGeneratorStatus.hh"
#include "mem/ruby/tester/DeterministicDriver.hh"
#include "mem/ruby/tester/Tester_Globals.hh"
//#include "DMAController.hh"
#include "mem/ruby/libruby.hh"

DetermInvGenerator::DetermInvGenerator(NodeID node, DeterministicDriver& driver) :
  m_driver(driver)
{
  m_status = DetermInvGeneratorStatus_Thinking;
  m_last_transition = 0;
  m_node = node;
  m_address = Address(9999);  // initiate to a NULL value
  m_counter = 0;
  // don't know exactly when this node needs to request so just guess randomly
  m_driver.eventQueue->scheduleEvent(this, 1+(random() % 200));
}

DetermInvGenerator::~DetermInvGenerator()
{
}

void DetermInvGenerator::wakeup()
{
  DEBUG_EXPR(TESTER_COMP, MedPrio, m_node);
  DEBUG_EXPR(TESTER_COMP, MedPrio, m_status);

  // determine if this node is next for the load round robin request
  if (m_status == DetermInvGeneratorStatus_Thinking) {
    // is a load ready and waiting and are my transactions insync with global transactions
    if (m_driver.isLoadReady(m_node) && m_counter == m_driver.getStoresCompleted()) {
      pickLoadAddress();
      m_status = DetermInvGeneratorStatus_Load_Pending;  // Load Pending
      m_last_transition = m_driver.eventQueue->getTime();
      initiateLoad();  // GETS
    } else { // I'll check again later
      m_driver.eventQueue->scheduleEvent(this, thinkTime());
    }
  } else if (m_status == DetermInvGeneratorStatus_Load_Complete) {    
    if (m_driver.isStoreReady(m_node, m_address))   {  // do a store in this transaction or start the next one
      if (m_driver.isLoadReady((0), m_address)) {  // everyone is in S for this address i.e. back to node 0
        m_status = DetermInvGeneratorStatus_Store_Pending;
        m_last_transition = m_driver.eventQueue->getTime();
        initiateStore();  // GETX
      } else {  // I'm next, I just have to wait for all loads to complete
        m_driver.eventQueue->scheduleEvent(this, thinkTime());
      }
    } else {  // I'm not next to store, go back to thinking
      m_status = DetermInvGeneratorStatus_Thinking;
      m_driver.eventQueue->scheduleEvent(this, thinkTime());
    }
  } else {
    WARN_EXPR(m_status);
    ERROR_MSG("Invalid status");
  }

}

void DetermInvGenerator::performCallback(NodeID proc, Address address)
{
  assert(proc == m_node);
  assert(address == m_address);  

  DEBUG_EXPR(TESTER_COMP, LowPrio, proc);
  DEBUG_EXPR(TESTER_COMP, LowPrio, m_status);
  DEBUG_EXPR(TESTER_COMP, LowPrio, address);

  if (m_status == DetermInvGeneratorStatus_Load_Pending) { 
    m_driver.recordLoadLatency(m_driver.eventQueue->getTime() - m_last_transition);
    //NodeID firstByte = data.readByte();  // dummy read

    m_driver.loadCompleted(m_node, address);

    if (!m_driver.isStoreReady(m_node, m_address))  {  // if we don't have to store, we are done for this transaction
      m_counter++;
    }
    if (m_counter < m_driver.m_tester_length) {
      m_status = DetermInvGeneratorStatus_Load_Complete;
      m_last_transition = m_driver.eventQueue->getTime();
      m_driver.eventQueue->scheduleEvent(this, waitTime());
    } else {
      m_driver.reportDone();
      m_status = DetermInvGeneratorStatus_Done;
      m_last_transition = m_driver.eventQueue->getTime();
    } 

  } else if (m_status == DetermInvGeneratorStatus_Store_Pending) { 
    m_driver.recordStoreLatency(m_driver.eventQueue->getTime() - m_last_transition);
    //data.writeByte(m_node);
    m_driver.storeCompleted(m_node, address);  // advance the store queue

    m_counter++;
    if (m_counter < m_driver.m_tester_length) {
      m_status = DetermInvGeneratorStatus_Thinking;
      m_last_transition = m_driver.eventQueue->getTime();
      m_driver.eventQueue->scheduleEvent(this, waitTime());
    } else {
      m_driver.reportDone();
      m_status = DetermInvGeneratorStatus_Done;
      m_last_transition = m_driver.eventQueue->getTime();
    }     
  } else {
    WARN_EXPR(m_status);
    ERROR_MSG("Invalid status");
  }

  DEBUG_EXPR(TESTER_COMP, LowPrio, proc);
  DEBUG_EXPR(TESTER_COMP, LowPrio, m_status);
  DEBUG_EXPR(TESTER_COMP, LowPrio, address);

}

int DetermInvGenerator::thinkTime() const
{
  return m_driver.m_think_time;
}

int DetermInvGenerator::waitTime() const
{
  return m_driver.m_wait_time;
}

int DetermInvGenerator::holdTime() const
{
  assert(0);
}

void DetermInvGenerator::pickLoadAddress()
{
  assert(m_status == DetermInvGeneratorStatus_Thinking);

  m_address = m_driver.getNextLoadAddr(m_node);
}

void DetermInvGenerator::initiateLoad()
{
  DEBUG_MSG(TESTER_COMP, MedPrio, "initiating Load");
  // sequencer()->makeRequest(CacheMsg(m_address, m_address, CacheRequestType_LD, Address(1), AccessModeType_UserMode, 1, PrefetchBit_No, Address(0), 0 /* only 1 SMT thread */)); 
  uint8_t * read_data = new uint8_t[64];

  char name [] = "Sequencer_";
  char port_name [13];
  sprintf(port_name, "%s%d", name, m_node);

  int64_t request_id = libruby_issue_request(libruby_get_port_by_name(port_name), RubyRequest(m_address.getAddress(), read_data, 64, 0, RubyRequestType_LD, RubyAccessMode_Supervisor));

  //delete [] read_data;

  ASSERT(m_driver.requests.find(request_id) == m_driver.requests.end()); 
  m_driver.requests.insert(make_pair(request_id, make_pair(m_node, m_address)));

}

void DetermInvGenerator::initiateStore()
{
  DEBUG_MSG(TESTER_COMP, MedPrio, "initiating Store");
  // sequencer()->makeRequest(CacheMsg(m_address, m_address, CacheRequestType_ST, Address(3), AccessModeType_UserMode, 1, PrefetchBit_No, Address(0), 0 /* only 1 SMT thread */)); 
  uint8_t *write_data = new uint8_t[64];
  for(int i=0; i < 64; i++) {
      write_data[i] = m_node;
  }

  char name [] = "Sequencer_";
  char port_name [13];
  sprintf(port_name, "%s%d", name, m_node);

  int64_t request_id = libruby_issue_request(libruby_get_port_by_name(port_name), RubyRequest(m_address.getAddress(), write_data, 64, 0, RubyRequestType_ST, RubyAccessMode_Supervisor));

  //delete [] write_data;

  ASSERT(m_driver.requests.find(request_id) == m_driver.requests.end()); 
  m_driver.requests.insert(make_pair(request_id, make_pair(m_node, m_address)));

}

void DetermInvGenerator::print(ostream& out) const
{
  out << "[DetermInvGenerator]" << endl;
}

