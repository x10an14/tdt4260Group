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

#ifndef __MEM_RUBY_NETWORK_GARNET_FLEXIBLE_PIPELINE_GARNET_NETWORK_HH__
#define __MEM_RUBY_NETWORK_GARNET_FLEXIBLE_PIPELINE_GARNET_NETWORK_HH__

#include <iostream>
#include <vector>

#include "mem/ruby/network/garnet/NetworkHeader.hh"
#include "mem/ruby/network/garnet/BaseGarnetNetwork.hh"
#include "mem/ruby/network/Network.hh"
#include "params/GarnetNetwork.hh"

class NetworkInterface;
class MessageBuffer;
class Router;
class Topology;
class NetDest;
class NetworkLink;

class GarnetNetwork : public BaseGarnetNetwork
{
  public:
    typedef GarnetNetworkParams Params;
    GarnetNetwork(const Params *p);

    ~GarnetNetwork();

    void init();

    // returns the queue requested for the given component
    MessageBuffer* getToNetQueue(NodeID id, bool ordered, int network_num);
    MessageBuffer* getFromNetQueue(NodeID id, bool ordered, int network_num);

    void clearStats();
    void printStats(std::ostream& out) const;
    void printConfig(std::ostream& out) const;
    void print(std::ostream& out) const;

    bool isVNetOrdered(int vnet) { return m_ordered[vnet]; }
    bool validVirtualNetwork(int vnet) { return m_in_use[vnet]; }

    Time getRubyStartTime();
    int getNumNodes(){ return m_nodes; }

    void reset();

    // Methods used by Topology to setup the network
    void makeOutLink(SwitchID src, NodeID dest,
        const NetDest& routing_table_entry, int link_latency,
        int link_weight,  int bw_multiplier, bool isReconfiguration);
    void makeInLink(SwitchID src, NodeID dest,
        const NetDest& routing_table_entry, int link_latency,
        int bw_multiplier, bool isReconfiguration);
    void makeInternalLink(SwitchID src, NodeID dest,
        const NetDest& routing_table_entry, int link_latency,
        int link_weight, int bw_multiplier, bool isReconfiguration);

  private:
    void checkNetworkAllocation(NodeID id, bool ordered, int network_num);

    GarnetNetwork(const GarnetNetwork& obj);
    GarnetNetwork& operator=(const GarnetNetwork& obj);

    // int m_virtual_networks;
    // int m_nodes;

    std::vector<bool> m_in_use;
    std::vector<bool> m_ordered;

    std::vector<std::vector<MessageBuffer*> > m_toNetQueues;
    std::vector<std::vector<MessageBuffer*> > m_fromNetQueues;

    std::vector<Router *> m_router_ptr_vector;   // All Routers in Network
    std::vector<NetworkLink *> m_link_ptr_vector; // All links in network
    std::vector<NetworkInterface *> m_ni_ptr_vector; // All NI's in Network

    // Topology* m_topology_ptr;
    Time m_ruby_start;
};

inline std::ostream&
operator<<(std::ostream& out, const GarnetNetwork& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

#endif // __MEM_RUBY_NETWORK_GARNET_FLEXIBLE_PIPELINE_GARNET_NETWORK_HH__

