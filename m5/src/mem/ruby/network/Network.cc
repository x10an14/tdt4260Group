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

#include "mem/protocol/MachineType.hh"
#include "mem/ruby/network/Network.hh"
#include "mem/ruby/network/simple/Topology.hh"

Network::Network(const Params *p)
    : SimObject(p)
{
    m_virtual_networks = p->number_of_virtual_networks;
    m_topology_ptr = p->topology;
    m_buffer_size = p->buffer_size;
    m_endpoint_bandwidth = p->endpoint_bandwidth;
    m_adaptive_routing = p->adaptive_routing;
    m_link_latency = p->link_latency;
    m_control_msg_size = p->control_msg_size;

    // Total nodes/controllers in network
    // Must make sure this is called after the State Machine constructors
    m_nodes = MachineType_base_number(MachineType_NUM);
    assert(m_nodes != 0);

    assert(m_virtual_networks != 0);
    assert(m_topology_ptr != NULL);

    // Initialize the controller's network pointers
    m_topology_ptr->initNetworkPtr(this);
}

void
Network::init()
{
    m_data_msg_size = RubySystem::getBlockSizeBytes() + m_control_msg_size;
}

int
Network::MessageSizeType_to_int(MessageSizeType size_type)
{
    switch(size_type) {
      case MessageSizeType_Undefined:
        ERROR_MSG("Can't convert Undefined MessageSizeType to integer");
        break;
      case MessageSizeType_Control:
      case MessageSizeType_Request_Control:
      case MessageSizeType_Reissue_Control:
      case MessageSizeType_Response_Control:
      case MessageSizeType_Writeback_Control:
      case MessageSizeType_Forwarded_Control:
      case MessageSizeType_Invalidate_Control:
      case MessageSizeType_Unblock_Control:
      case MessageSizeType_Persistent_Control:
      case MessageSizeType_Completion_Control:
        return m_control_msg_size;
      case MessageSizeType_Data:
      case MessageSizeType_Response_Data:
      case MessageSizeType_ResponseLocal_Data:
      case MessageSizeType_ResponseL2hit_Data:
      case MessageSizeType_Writeback_Data:
        return m_data_msg_size;
      default:
        ERROR_MSG("Invalid range for type MessageSizeType");
        break;
    }
    return 0;
}

const std::vector<Throttle*>*
Network::getThrottles(NodeID id) const
{
    return NULL;
}
