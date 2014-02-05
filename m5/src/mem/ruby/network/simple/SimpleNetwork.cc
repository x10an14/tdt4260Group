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

#include "base/stl_helpers.hh"
#include "mem/protocol/MachineType.hh"
#include "mem/protocol/Protocol.hh"
#include "mem/protocol/TopologyType.hh"
#include "mem/ruby/buffers/MessageBuffer.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/network/simple/SimpleNetwork.hh"
#include "mem/ruby/network/simple/Switch.hh"
#include "mem/ruby/network/simple/Topology.hh"
#include "mem/ruby/profiler/Profiler.hh"
#include "mem/ruby/system/System.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

#if 0
// ***BIG HACK*** - This is actually code that _should_ be in Network.cc

// Note: Moved to Princeton Network
// calls new to abstract away from the network
Network*
Network::createNetwork(int nodes)
{
    return new SimpleNetwork(nodes);
}
#endif

SimpleNetwork::SimpleNetwork(const Params *p)
    : Network(p)
{
    // Note: the parent Network Object constructor is called before the
    // SimpleNetwork child constructor.  Therefore, the member variables
    // used below should already be initialized.

    m_endpoint_switches.resize(m_nodes);

    m_in_use.resize(m_virtual_networks);
    m_ordered.resize(m_virtual_networks);
    for (int i = 0; i < m_virtual_networks; i++) {
        m_in_use[i] = false;
        m_ordered[i] = false;
    }

    // Allocate to and from queues
    m_toNetQueues.resize(m_nodes);
    m_fromNetQueues.resize(m_nodes);
    for (int node = 0; node < m_nodes; node++) {
        m_toNetQueues[node].resize(m_virtual_networks);
        m_fromNetQueues[node].resize(m_virtual_networks);
        for (int j = 0; j < m_virtual_networks; j++) {
            m_toNetQueues[node][j] =
                new MessageBuffer(csprintf("toNet node %d j %d", node, j));
            m_fromNetQueues[node][j] =
                new MessageBuffer(csprintf("fromNet node %d j %d", node, j));
        }
    }
}

void
SimpleNetwork::init()
{
    Network::init();

    // The topology pointer should have already been initialized in
    // the parent class network constructor.
    assert(m_topology_ptr != NULL);
    int number_of_switches = m_topology_ptr->numSwitches();
    for (int i = 0; i < number_of_switches; i++) {
        m_switch_ptr_vector.push_back(new Switch(i, this));
    }

    // false because this isn't a reconfiguration
    m_topology_ptr->createLinks(this, false);
}

void
SimpleNetwork::reset()
{
    for (int node = 0; node < m_nodes; node++) {
        for (int j = 0; j < m_virtual_networks; j++) {
            m_toNetQueues[node][j]->clear();
            m_fromNetQueues[node][j]->clear();
        }
    }

    for(int i = 0; i < m_switch_ptr_vector.size(); i++){
        m_switch_ptr_vector[i]->clearBuffers();
    }
}

SimpleNetwork::~SimpleNetwork()
{
    for (int i = 0; i < m_nodes; i++) {
        deletePointers(m_toNetQueues[i]);
        deletePointers(m_fromNetQueues[i]);
    }
    deletePointers(m_switch_ptr_vector);
    deletePointers(m_buffers_to_free);
    // delete m_topology_ptr;
}

// From a switch to an endpoint node
void
SimpleNetwork::makeOutLink(SwitchID src, NodeID dest,
    const NetDest& routing_table_entry, int link_latency, int link_weight,
    int bw_multiplier, bool isReconfiguration)
{
    assert(dest < m_nodes);
    assert(src < m_switch_ptr_vector.size());
    assert(m_switch_ptr_vector[src] != NULL);

    if (isReconfiguration) {
        m_switch_ptr_vector[src]->reconfigureOutPort(routing_table_entry);
        return;
    }

    m_switch_ptr_vector[src]->addOutPort(m_fromNetQueues[dest],
        routing_table_entry, link_latency, bw_multiplier);
    m_endpoint_switches[dest] = m_switch_ptr_vector[src];
}

// From an endpoint node to a switch
void
SimpleNetwork::makeInLink(NodeID src, SwitchID dest,
    const NetDest& routing_table_entry, int link_latency, int bw_multiplier,
    bool isReconfiguration)
{
    assert(src < m_nodes);
    if (isReconfiguration) {
        // do nothing
        return;
    }

    m_switch_ptr_vector[dest]->addInPort(m_toNetQueues[src]);
}

// From a switch to a switch
void
SimpleNetwork::makeInternalLink(SwitchID src, SwitchID dest,
    const NetDest& routing_table_entry, int link_latency, int link_weight,
    int bw_multiplier, bool isReconfiguration)
{
    if (isReconfiguration) {
        m_switch_ptr_vector[src]->reconfigureOutPort(routing_table_entry);
        return;
    }

    // Create a set of new MessageBuffers
    std::vector<MessageBuffer*> queues;
    for (int i = 0; i < m_virtual_networks; i++) {
        // allocate a buffer
        MessageBuffer* buffer_ptr = new MessageBuffer;
        buffer_ptr->setOrdering(true);
        if (m_buffer_size > 0) {
            buffer_ptr->resize(m_buffer_size);
        }
        queues.push_back(buffer_ptr);
        // remember to deallocate it
        m_buffers_to_free.push_back(buffer_ptr);
    }
    // Connect it to the two switches
    m_switch_ptr_vector[dest]->addInPort(queues);
    m_switch_ptr_vector[src]->addOutPort(queues, routing_table_entry,
        link_latency, bw_multiplier);
}

void
SimpleNetwork::checkNetworkAllocation(NodeID id, bool ordered, int network_num)
{
    ASSERT(id < m_nodes);
    ASSERT(network_num < m_virtual_networks);

    if (ordered) {
        m_ordered[network_num] = true;
    }
    m_in_use[network_num] = true;
}

MessageBuffer*
SimpleNetwork::getToNetQueue(NodeID id, bool ordered, int network_num)
{
    checkNetworkAllocation(id, ordered, network_num);
    return m_toNetQueues[id][network_num];
}

MessageBuffer*
SimpleNetwork::getFromNetQueue(NodeID id, bool ordered, int network_num)
{
    checkNetworkAllocation(id, ordered, network_num);
    return m_fromNetQueues[id][network_num];
}

const std::vector<Throttle*>*
SimpleNetwork::getThrottles(NodeID id) const
{
    assert(id >= 0);
    assert(id < m_nodes);
    assert(m_endpoint_switches[id] != NULL);
    return m_endpoint_switches[id]->getThrottles();
}

void
SimpleNetwork::printStats(ostream& out) const
{
    out << endl;
    out << "Network Stats" << endl;
    out << "-------------" << endl;
    out << endl;
    for (int i = 0; i < m_switch_ptr_vector.size(); i++) {
        m_switch_ptr_vector[i]->printStats(out);
    }
    m_topology_ptr->printStats(out);
}

void
SimpleNetwork::clearStats()
{
    for (int i = 0; i < m_switch_ptr_vector.size(); i++) {
        m_switch_ptr_vector[i]->clearStats();
    }
    m_topology_ptr->clearStats();
}

void
SimpleNetwork::printConfig(ostream& out) const
{
    out << endl;
    out << "Network Configuration" << endl;
    out << "---------------------" << endl;
    out << "network: SIMPLE_NETWORK" << endl;
    out << "topology: " << m_topology_ptr->getName() << endl;
    out << endl;

    for (int i = 0; i < m_virtual_networks; i++) {
        out << "virtual_net_" << i << ": ";
        if (m_in_use[i]) {
            out << "active, ";
            if (m_ordered[i]) {
                out << "ordered" << endl;
            } else {
                out << "unordered" << endl;
            }
        } else {
            out << "inactive" << endl;
        }
    }
    out << endl;

    for(int i = 0; i < m_switch_ptr_vector.size(); i++) {
        m_switch_ptr_vector[i]->printConfig(out);
    }

    m_topology_ptr->printConfig(out);
}

void
SimpleNetwork::print(ostream& out) const
{
    out << "[SimpleNetwork]";
}


SimpleNetwork *
SimpleNetworkParams::create()
{
    return new SimpleNetwork(this);
}
