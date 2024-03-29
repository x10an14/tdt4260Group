/*
 * Copyright (c) 2008 Princeton University
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
 * Authors: Niket Agarwal
 */

#include "base/stl_helpers.hh"
#include "mem/ruby/network/garnet/flexible-pipeline/GarnetNetwork.hh"
#include "mem/protocol/MachineType.hh"
#include "mem/ruby/network/garnet/flexible-pipeline/NetworkInterface.hh"
#include "mem/ruby/buffers/MessageBuffer.hh"
#include "mem/ruby/network/garnet/flexible-pipeline/Router.hh"
#include "mem/ruby/network/simple/Topology.hh"
#include "mem/ruby/network/simple/SimpleNetwork.hh"
#include "mem/ruby/network/garnet/fixed-pipeline/GarnetNetwork_d.hh"
#include "mem/ruby/network/garnet/flexible-pipeline/NetworkLink.hh"
#include "mem/ruby/common/NetDest.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

GarnetNetwork::GarnetNetwork(const Params *p)
    : BaseGarnetNetwork(p)
{
    m_ruby_start = 0;

    // Allocate to and from queues

    // Queues that are getting messages from protocol
    m_toNetQueues.resize(m_nodes);

    // Queues that are feeding the protocol
    m_fromNetQueues.resize(m_nodes);

    m_in_use.resize(m_virtual_networks);
    m_ordered.resize(m_virtual_networks);
    for (int i = 0; i < m_virtual_networks; i++) {
        m_in_use[i] = false;
        m_ordered[i] = false;
    }

    for (int node = 0; node < m_nodes; node++) {
        //Setting number of virtual message buffers per Network Queue
        m_toNetQueues[node].resize(m_virtual_networks);
        m_fromNetQueues[node].resize(m_virtual_networks);

        // Instantiating the Message Buffers that
        // interact with the coherence protocol
        for (int j = 0; j < m_virtual_networks; j++) {
            m_toNetQueues[node][j] = new MessageBuffer();
            m_fromNetQueues[node][j] = new MessageBuffer();
        }
    }
}

void
GarnetNetwork::init()
{
    BaseGarnetNetwork::init();

    // Setup the network switches
    assert (m_topology_ptr!=NULL);

    int number_of_routers = m_topology_ptr->numSwitches();
    for (int i=0; i<number_of_routers; i++) {
        m_router_ptr_vector.push_back(new Router(i, this));
    }

    for (int i=0; i < m_nodes; i++) {
        NetworkInterface *ni = new NetworkInterface(i, m_virtual_networks,
                                                    this);
        ni->addNode(m_toNetQueues[i], m_fromNetQueues[i]);
        m_ni_ptr_vector.push_back(ni);
    }

    // false because this isn't a reconfiguration :
    m_topology_ptr->createLinks(this, false);
}

GarnetNetwork::~GarnetNetwork()
{
    for (int i = 0; i < m_nodes; i++) {
        deletePointers(m_toNetQueues[i]);
        deletePointers(m_fromNetQueues[i]);
    }
    deletePointers(m_router_ptr_vector);
    deletePointers(m_ni_ptr_vector);
    deletePointers(m_link_ptr_vector);
    delete m_topology_ptr;
}

void
GarnetNetwork::reset()
{
    for (int node = 0; node < m_nodes; node++) {
        for (int j = 0; j < m_virtual_networks; j++) {
            m_toNetQueues[node][j]->clear();
            m_fromNetQueues[node][j]->clear();
        }
    }
}

void
GarnetNetwork::makeInLink(NodeID src, SwitchID dest,
    const NetDest& routing_table_entry, int link_latency, int bw_multiplier,
    bool isReconfiguration)
{
    assert(src < m_nodes);

    if (!isReconfiguration) {
        NetworkLink *net_link = new NetworkLink(m_link_ptr_vector.size(),
                                                link_latency, this);
        m_link_ptr_vector.push_back(net_link);
        m_router_ptr_vector[dest]->addInPort(net_link);
        m_ni_ptr_vector[src]->addOutPort(net_link);
    }         else {
        ERROR_MSG("Fatal Error:: Reconfiguration not allowed here");
        // do nothing
    }
}

void
GarnetNetwork::makeOutLink(SwitchID src, NodeID dest,
    const NetDest& routing_table_entry, int link_latency, int link_weight,
    int bw_multiplier, bool isReconfiguration)
{
    assert(dest < m_nodes);
    assert(src < m_router_ptr_vector.size());
    assert(m_router_ptr_vector[src] != NULL);

    if (!isReconfiguration) {
        NetworkLink *net_link = new NetworkLink(m_link_ptr_vector.size(),
                                                link_latency, this);
        m_link_ptr_vector.push_back(net_link);
        m_router_ptr_vector[src]->addOutPort(net_link, routing_table_entry,
                                             link_weight);
        m_ni_ptr_vector[dest]->addInPort(net_link);
    }         else {
        ERROR_MSG("Fatal Error:: Reconfiguration not allowed here");
        //do nothing
    }
}

void
GarnetNetwork::makeInternalLink(SwitchID src, SwitchID dest,
    const NetDest& routing_table_entry, int link_latency, int link_weight,
    int bw_multiplier, bool isReconfiguration)
{
    if (!isReconfiguration) {
        NetworkLink *net_link = new NetworkLink(m_link_ptr_vector.size(),
                                                link_latency, this);
        m_link_ptr_vector.push_back(net_link);
        m_router_ptr_vector[dest]->addInPort(net_link);
        m_router_ptr_vector[src]->addOutPort(net_link, routing_table_entry,
                                             link_weight);
    }         else {
        ERROR_MSG("Fatal Error:: Reconfiguration not allowed here");
        // do nothing
    }

}

void
GarnetNetwork::checkNetworkAllocation(NodeID id, bool ordered,
    int network_num)
{
    printf ("id = %i, m_nodes = %i \n", id, m_nodes);
    ASSERT(id < m_nodes);
    ASSERT(network_num < m_virtual_networks);

    if (ordered) {
        m_ordered[network_num] = true;
    }
    m_in_use[network_num] = true;
}

MessageBuffer*
GarnetNetwork::getToNetQueue(NodeID id, bool ordered, int network_num)
{
    checkNetworkAllocation(id, ordered, network_num);
    return m_toNetQueues[id][network_num];
}

MessageBuffer*
GarnetNetwork::getFromNetQueue(NodeID id, bool ordered, int network_num)
{
    checkNetworkAllocation(id, ordered, network_num);
    return m_fromNetQueues[id][network_num];
}

void
GarnetNetwork::clearStats()
{
    m_ruby_start = g_eventQueue_ptr->getTime();
}

Time
GarnetNetwork::getRubyStartTime()
{
    return m_ruby_start;
}

void
GarnetNetwork::printStats(ostream& out) const
{
    double average_link_utilization = 0;
    vector<double> average_vc_load;
    average_vc_load.resize(m_virtual_networks*m_vcs_per_class);

    for (int i = 0; i < m_virtual_networks*m_vcs_per_class; i++) {
        average_vc_load[i] = 0;
    }

    out << endl;
    out << "Network Stats" << endl;
    out << "-------------" << endl;
    out << endl;
    for (int i = 0; i < m_link_ptr_vector.size(); i++) {
        average_link_utilization +=
            m_link_ptr_vector[i]->getLinkUtilization();
        vector<int> vc_load = m_link_ptr_vector[i]->getVcLoad();
        for (int j = 0; j < vc_load.size(); j++) {
            assert(vc_load.size() == m_vcs_per_class*m_virtual_networks);
            average_vc_load[j] += vc_load[j];
        }
    }
    average_link_utilization =
        average_link_utilization/m_link_ptr_vector.size();
    out << "Average Link Utilization :: " << average_link_utilization <<
           " flits/cycle" <<endl;
    out << "-------------" << endl;

    for (int i = 0; i < m_vcs_per_class*m_virtual_networks; i++) {
        average_vc_load[i] = (double(average_vc_load[i]) /
            (double(g_eventQueue_ptr->getTime()) - m_ruby_start));
        out << "Average VC Load [" << i << "] = " << average_vc_load[i] <<
               " flits/cycle" << endl;
    }
    out << "-------------" << endl;
    m_topology_ptr->printStats(out);
}

void
GarnetNetwork::printConfig(ostream& out) const
{
    out << endl;
    out << "Network Configuration" << endl;
    out << "---------------------" << endl;
    out << "network: GARNET_NETWORK" << endl;
    out << "topology: " << m_topology_ptr->getName() << endl;
    out << endl;

    for (int i = 0; i < m_virtual_networks; i++) {
        out << "virtual_net_" << i << ": ";
        if (m_in_use[i]) {
            out << "active, ";
            if (m_ordered[i]) {
                out << "ordered" << endl;
            }                         else {
                out << "unordered" << endl;
            }
        }                 else {
            out << "inactive" << endl;
        }
    }
    out << endl;

    for (int i = 0; i < m_ni_ptr_vector.size(); i++) {
        m_ni_ptr_vector[i]->printConfig(out);
    }
    for (int i = 0; i < m_router_ptr_vector.size(); i++) {
        m_router_ptr_vector[i]->printConfig(out);
    }
    m_topology_ptr->printConfig(out);
}

void
GarnetNetwork::print(ostream& out) const
{
    out << "[GarnetNetwork]";
}

GarnetNetwork *
GarnetNetworkParams::create()
{
    return new GarnetNetwork(this);
}
