/*
 * Copyright (c) 2006 The Regents of The University of Michigan
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
 * Authors: Ali Saidi
 */

/**
 * @file
 * Definition of a bus object.
 */

#include <algorithm>
#include <limits>

#include "base/misc.hh"
#include "base/trace.hh"
#include "mem/bus.hh"

Port *
Bus::getPort(const std::string &if_name, int idx)
{
    if (if_name == "default") {
        if (defaultPort == NULL) {
            defaultPort = new BusPort(csprintf("%s-default",name()), this,
                                      defaultId);
            cachedBlockSizeValid = false;
            return defaultPort;
        } else
            fatal("Default port already set\n");
    }
    int id;
    if (if_name == "functional") {
        if (!funcPort) {
            id = maxId++;
            funcPort = new BusPort(csprintf("%s-p%d-func", name(), id), this, id);
            funcPortId = id;
            interfaces[id] = funcPort;
        }
        return funcPort;
    }

    // if_name ignored?  forced to be empty?
    id = maxId++;
    assert(maxId < std::numeric_limits<typeof(maxId)>::max());
    BusPort *bp = new BusPort(csprintf("%s-p%d", name(), id), this, id);
    interfaces[id] = bp;
    cachedBlockSizeValid = false;
    return bp;
}

void
Bus::deletePortRefs(Port *p)
{

    BusPort *bp =  dynamic_cast<BusPort*>(p);
    if (bp == NULL)
        panic("Couldn't convert Port* to BusPort*\n");
    // If this is our one functional port
    if (funcPort == bp)
        return;
    interfaces.erase(bp->getId());
    clearBusCache();
    delete bp;
}

/** Get the ranges of anyone other buses that we are connected to. */
void
Bus::init()
{
    m5::hash_map<short,BusPort*>::iterator intIter;

    for (intIter = interfaces.begin(); intIter != interfaces.end(); intIter++)
        intIter->second->sendStatusChange(Port::RangeChange);
}

Bus::BusFreeEvent::BusFreeEvent(Bus *_bus)
    : bus(_bus)
{}

void
Bus::BusFreeEvent::process()
{
    bus->recvRetry(-1);
}

const char *
Bus::BusFreeEvent::description() const
{
    return "bus became available";
}

Tick
Bus::calcPacketTiming(PacketPtr pkt)
{
    // Bring tickNextIdle up to the present tick.
    // There is some potential ambiguity where a cycle starts, which
    // might make a difference when devices are acting right around a
    // cycle boundary. Using a < allows things which happen exactly on
    // a cycle boundary to take up only the following cycle. Anything
    // that happens later will have to "wait" for the end of that
    // cycle, and then start using the bus after that.
    if (tickNextIdle < curTick) {
        tickNextIdle = curTick;
        if (tickNextIdle % clock != 0)
            tickNextIdle = curTick - (curTick % clock) + clock;
    }

    Tick headerTime = tickNextIdle + headerCycles * clock;

    // The packet will be sent. Figure out how long it occupies the bus, and
    // how much of that time is for the first "word", aka bus width.
    int numCycles = 0;
    if (pkt->hasData()) {
        // If a packet has data, it needs ceil(size/width) cycles to send it
        int dataSize = pkt->getSize();
        numCycles += dataSize/width;
        if (dataSize % width)
            numCycles++;
    }

    // The first word will be delivered after the current tick, the delivery
    // of the address if any, and one bus cycle to deliver the data
    pkt->firstWordTime = headerTime + clock;

    pkt->finishTime = headerTime + numCycles * clock;

    return headerTime;
}

void Bus::occupyBus(Tick until)
{
    if (until == 0) {
        // shortcut for express snoop packets
        return;
    }

    tickNextIdle = until;
    reschedule(busIdle, tickNextIdle, true);

    DPRINTF(Bus, "The bus is now occupied from tick %d to %d\n",
            curTick, tickNextIdle);
}

/** Function called by the port when the bus is receiving a Timing
 * transaction.*/
bool
Bus::recvTiming(PacketPtr pkt)
{
    short src = pkt->getSrc();

    BusPort *src_port;
    if (src == defaultId)
        src_port = defaultPort;
    else {
        src_port = checkBusCache(src);
        if (src_port == NULL) {
            src_port = interfaces[src];
            updateBusCache(src, src_port);
        }
    }

    // If the bus is busy, or other devices are in line ahead of the current
    // one, put this device on the retry list.
    if (!pkt->isExpressSnoop() &&
        (tickNextIdle > curTick ||
         (retryList.size() && (!inRetry || src_port != retryList.front()))))
    {
        addToRetryList(src_port);
        DPRINTF(Bus, "recvTiming: src %d dst %d %s 0x%x BUSY\n",
                src, pkt->getDest(), pkt->cmdString(), pkt->getAddr());
        return false;
    }

    DPRINTF(Bus, "recvTiming: src %d dst %d %s 0x%x\n",
            src, pkt->getDest(), pkt->cmdString(), pkt->getAddr());

    Tick headerFinishTime = pkt->isExpressSnoop() ? 0 : calcPacketTiming(pkt);
    Tick packetFinishTime = pkt->isExpressSnoop() ? 0 : pkt->finishTime;

    short dest = pkt->getDest();
    int dest_port_id;
    Port *dest_port;

    if (dest == Packet::Broadcast) {
        dest_port_id = findPort(pkt->getAddr());
        dest_port = (dest_port_id == defaultId) ?
            defaultPort : interfaces[dest_port_id];
        SnoopIter s_end = snoopPorts.end();
        for (SnoopIter s_iter = snoopPorts.begin(); s_iter != s_end; s_iter++) {
            BusPort *p = *s_iter;
            if (p != dest_port && p != src_port) {
                // cache is not allowed to refuse snoop
                bool success M5_VAR_USED = p->sendTiming(pkt);
                assert(success);
            }
        }
    } else {
        assert(dest < maxId);
        assert(dest != src); // catch infinite loops
        dest_port_id = dest;
        if (dest_port_id == defaultId)
            dest_port = defaultPort;
        else {
            dest_port = checkBusCache(dest);
            if (dest_port == NULL) {
                dest_port = interfaces[dest_port_id];
            // updateBusCache(dest_port_id, dest_port);
            }
        }
        dest_port = (dest_port_id == defaultId) ?
            defaultPort : interfaces[dest_port_id];
    }

    if (dest_port_id == src) {
        // Must be forwarded snoop up from below...
        assert(dest == Packet::Broadcast);
    } else {
        // send to actual target
        if (!dest_port->sendTiming(pkt))  {
            // Packet not successfully sent. Leave or put it on the retry list.
            // illegal to block responses... can lead to deadlock
            assert(!pkt->isResponse());
            // It's also illegal to force a transaction to retry after
            // someone else has committed to respond.
            assert(!pkt->memInhibitAsserted());
            DPRINTF(Bus, "recvTiming: src %d dst %d %s 0x%x TGT RETRY\n",
                    src, pkt->getDest(), pkt->cmdString(), pkt->getAddr());
            addToRetryList(src_port);
            occupyBus(headerFinishTime);
            return false;
        }
        // send OK, fall through... pkt may have been deleted by
        // target at this point, so it should *not* be referenced
        // again.  We'll set it to NULL here just to be safe.
        pkt = NULL;
    }

    occupyBus(packetFinishTime);

    // Packet was successfully sent.
    // Also take care of retries
    if (inRetry) {
        DPRINTF(Bus, "Remove retry from list %d\n", src);
        retryList.front()->onRetryList(false);
        retryList.pop_front();
        inRetry = false;
    }
    return true;
}

void
Bus::recvRetry(int id)
{
    // If there's anything waiting, and the bus isn't busy...
    if (retryList.size() && curTick >= tickNextIdle) {
        //retryingPort = retryList.front();
        inRetry = true;
        DPRINTF(Bus, "Sending a retry to %s\n", retryList.front()->getPeer()->name());
        retryList.front()->sendRetry();
        // If inRetry is still true, sendTiming wasn't called
        if (inRetry)
        {
            retryList.front()->onRetryList(false);
            retryList.pop_front();
            inRetry = false;

            //Bring tickNextIdle up to the present
            while (tickNextIdle < curTick)
                tickNextIdle += clock;

            //Burn a cycle for the missed grant.
            tickNextIdle += clock;

            reschedule(busIdle, tickNextIdle, true);
        }
    }
    //If we weren't able to drain before, we might be able to now.
    if (drainEvent && retryList.size() == 0 && curTick >= tickNextIdle) {
        drainEvent->process();
        // Clear the drain event once we're done with it.
        drainEvent = NULL;
    }
}

int
Bus::findPort(Addr addr)
{
    /* An interval tree would be a better way to do this. --ali. */
    int dest_id = -1;

    dest_id = checkPortCache(addr);
    if (dest_id == -1) {
        PortIter i = portMap.find(RangeSize(addr,1));
        if (i != portMap.end()) {
            dest_id = i->second;
            updatePortCache(dest_id, i->first.start, i->first.end);
        }
    }

    // Check if this matches the default range
    if (dest_id == -1) {
        AddrRangeIter a_end = defaultRange.end();
        for (AddrRangeIter i = defaultRange.begin(); i != a_end; i++) {
            if (*i == addr) {
                DPRINTF(Bus, "  found addr %#llx on default\n", addr);
                return defaultId;
            }
        }

        if (responderSet) {
            panic("Unable to find destination for addr (user set default "
                  "responder): %#llx\n", addr);
        } else {
            DPRINTF(Bus, "Unable to find destination for addr: %#llx, will use "
                    "default port\n", addr);

            return defaultId;
        }
    }

    return dest_id;
}


/** Function called by the port when the bus is receiving a Atomic
 * transaction.*/
Tick
Bus::recvAtomic(PacketPtr pkt)
{
    DPRINTF(Bus, "recvAtomic: packet src %d dest %d addr 0x%x cmd %s\n",
            pkt->getSrc(), pkt->getDest(), pkt->getAddr(), pkt->cmdString());
    assert(pkt->getDest() == Packet::Broadcast);
    assert(pkt->isRequest());

    // Variables for recording original command and snoop response (if
    // any)... if a snooper respondes, we will need to restore
    // original command so that additional snoops can take place
    // properly
    MemCmd orig_cmd = pkt->cmd;
    MemCmd snoop_response_cmd = MemCmd::InvalidCmd;
    Tick snoop_response_latency = 0;
    int orig_src = pkt->getSrc();

    int target_port_id = findPort(pkt->getAddr());
    BusPort *target_port;
    if (target_port_id == defaultId)
        target_port = defaultPort;
    else {
      target_port = checkBusCache(target_port_id);
      if (target_port == NULL) {
          target_port = interfaces[target_port_id];
          updateBusCache(target_port_id, target_port);
      }
    }

    SnoopIter s_end = snoopPorts.end();
    for (SnoopIter s_iter = snoopPorts.begin(); s_iter != s_end; s_iter++) {
        BusPort *p = *s_iter;
        // same port should not have both target addresses and snooping
        assert(p != target_port);
        if (p->getId() != pkt->getSrc()) {
            Tick latency = p->sendAtomic(pkt);
            if (pkt->isResponse()) {
                // response from snoop agent
                assert(pkt->cmd != orig_cmd);
                assert(pkt->memInhibitAsserted());
                // should only happen once
                assert(snoop_response_cmd == MemCmd::InvalidCmd);
                // save response state
                snoop_response_cmd = pkt->cmd;
                snoop_response_latency = latency;
                // restore original packet state for remaining snoopers
                pkt->cmd = orig_cmd;
                pkt->setSrc(orig_src);
                pkt->setDest(Packet::Broadcast);
            }
        }
    }

    Tick response_latency = 0;

    // we can get requests sent up from the memory side of the bus for
    // snooping... don't send them back down!
    if (target_port_id != pkt->getSrc()) {
        response_latency = target_port->sendAtomic(pkt);
    }

    // if we got a response from a snooper, restore it here
    if (snoop_response_cmd != MemCmd::InvalidCmd) {
        // no one else should have responded
        assert(!pkt->isResponse());
        assert(pkt->cmd == orig_cmd);
        pkt->cmd = snoop_response_cmd;
        response_latency = snoop_response_latency;
    }

    // why do we have this packet field and the return value both???
    pkt->finishTime = curTick + response_latency;
    return response_latency;
}

/** Function called by the port when the bus is receiving a Functional
 * transaction.*/
void
Bus::recvFunctional(PacketPtr pkt)
{
    if (!pkt->isPrint()) {
        // don't do DPRINTFs on PrintReq as it clutters up the output
        DPRINTF(Bus,
                "recvFunctional: packet src %d dest %d addr 0x%x cmd %s\n",
                pkt->getSrc(), pkt->getDest(), pkt->getAddr(),
                pkt->cmdString());
    }
    assert(pkt->getDest() == Packet::Broadcast);

    int port_id = findPort(pkt->getAddr());
    Port *port = (port_id == defaultId) ? defaultPort : interfaces[port_id];
    // The packet may be changed by another bus on snoops, restore the
    // id after each
    int src_id = pkt->getSrc();

    assert(pkt->isRequest()); // hasn't already been satisfied

    SnoopIter s_end = snoopPorts.end();
    for (SnoopIter s_iter = snoopPorts.begin(); s_iter != s_end; s_iter++) {
        BusPort *p = *s_iter;
        if (p != port && p->getId() != src_id) {
            p->sendFunctional(pkt);
        }
        if (pkt->isResponse()) {
            break;
        }
        pkt->setSrc(src_id);
    }

    // If the snooping hasn't found what we were looking for, keep going.
    if (!pkt->isResponse() && port_id != pkt->getSrc()) {
        port->sendFunctional(pkt);
    }
}

/** Function called by the port when the bus is receiving a status change.*/
void
Bus::recvStatusChange(Port::Status status, int id)
{
    AddrRangeList ranges;
    bool snoops;
    AddrRangeIter iter;

    if (inRecvStatusChange.count(id))
        return;
    inRecvStatusChange.insert(id);

    assert(status == Port::RangeChange &&
           "The other statuses need to be implemented.");

    DPRINTF(BusAddrRanges, "received RangeChange from device id %d\n", id);

    clearPortCache();
    if (id == defaultId) {
        defaultRange.clear();
        // Only try to update these ranges if the user set a default responder.
        if (responderSet) {
            defaultPort->getPeerAddressRanges(ranges, snoops);
            assert(snoops == false);
            for(iter = ranges.begin(); iter != ranges.end(); iter++) {
                defaultRange.push_back(*iter);
                DPRINTF(BusAddrRanges, "Adding range %#llx - %#llx for default range\n",
                        iter->start, iter->end);
            }
        }
    } else {

        assert((id < maxId && id >= 0) || id == defaultId);
        BusPort *port = interfaces[id];

        // Clean out any previously existent ids
        for (PortIter portIter = portMap.begin();
             portIter != portMap.end(); ) {
            if (portIter->second == id)
                portMap.erase(portIter++);
            else
                portIter++;
        }

        for (SnoopIter s_iter = snoopPorts.begin();
             s_iter != snoopPorts.end(); ) {
            if ((*s_iter)->getId() == id)
                s_iter = snoopPorts.erase(s_iter);
            else
                s_iter++;
        }

        port->getPeerAddressRanges(ranges, snoops);

        if (snoops) {
            DPRINTF(BusAddrRanges, "Adding id %d to snoop list\n", id);
            snoopPorts.push_back(port);
        }

        for (iter = ranges.begin(); iter != ranges.end(); iter++) {
            DPRINTF(BusAddrRanges, "Adding range %#llx - %#llx for id %d\n",
                    iter->start, iter->end, id);
            if (portMap.insert(*iter, id) == portMap.end()) {
                int conflict_id = portMap.find(*iter)->second;
                fatal("%s has two ports with same range:\n\t%s\n\t%s\n",
                      name(), interfaces[id]->getPeer()->name(),
                      interfaces[conflict_id]->getPeer()->name());
            }
        }
    }
    DPRINTF(MMU, "port list has %d entries\n", portMap.size());

    // tell all our peers that our address range has changed.
    // Don't tell the device that caused this change, it already knows
    m5::hash_map<short,BusPort*>::iterator intIter;

    for (intIter = interfaces.begin(); intIter != interfaces.end(); intIter++)
        if (intIter->first != id && intIter->first != funcPortId)
            intIter->second->sendStatusChange(Port::RangeChange);

    if (id != defaultId && defaultPort)
        defaultPort->sendStatusChange(Port::RangeChange);
    inRecvStatusChange.erase(id);
}

void
Bus::addressRanges(AddrRangeList &resp, bool &snoop, int id)
{
    resp.clear();
    snoop = false;

    DPRINTF(BusAddrRanges, "received address range request, returning:\n");

    for (AddrRangeIter dflt_iter = defaultRange.begin();
         dflt_iter != defaultRange.end(); dflt_iter++) {
        resp.push_back(*dflt_iter);
        DPRINTF(BusAddrRanges, "  -- Dflt: %#llx : %#llx\n",dflt_iter->start,
                dflt_iter->end);
    }
    for (PortIter portIter = portMap.begin();
         portIter != portMap.end(); portIter++) {
        bool subset = false;
        for (AddrRangeIter dflt_iter = defaultRange.begin();
             dflt_iter != defaultRange.end(); dflt_iter++) {
            if ((portIter->first.start < dflt_iter->start &&
                portIter->first.end >= dflt_iter->start) ||
               (portIter->first.start < dflt_iter->end &&
                portIter->first.end >= dflt_iter->end))
                fatal("Devices can not set ranges that itersect the default set\
                        but are not a subset of the default set.\n");
            if (portIter->first.start >= dflt_iter->start &&
                portIter->first.end <= dflt_iter->end) {
                subset = true;
                DPRINTF(BusAddrRanges, "  -- %#llx : %#llx is a SUBSET\n",
                    portIter->first.start, portIter->first.end);
            }
        }
        if (portIter->second != id && !subset) {
            resp.push_back(portIter->first);
            DPRINTF(BusAddrRanges, "  -- %#llx : %#llx\n",
                    portIter->first.start, portIter->first.end);
        }
    }

    for (SnoopIter s_iter = snoopPorts.begin(); s_iter != snoopPorts.end();
         s_iter++) {
        if ((*s_iter)->getId() != id) {
            snoop = true;
            break;
        }
    }
}

unsigned
Bus::findBlockSize(int id)
{
    if (cachedBlockSizeValid)
        return cachedBlockSize;

    unsigned max_bs = 0;

    PortIter p_end = portMap.end();
    for (PortIter p_iter = portMap.begin(); p_iter != p_end; p_iter++) {
        unsigned tmp_bs = interfaces[p_iter->second]->peerBlockSize();
        if (tmp_bs > max_bs)
            max_bs = tmp_bs;
    }
    SnoopIter s_end = snoopPorts.end();
    for (SnoopIter s_iter = snoopPorts.begin(); s_iter != s_end; s_iter++) {
        unsigned tmp_bs = (*s_iter)->peerBlockSize();
        if (tmp_bs > max_bs)
            max_bs = tmp_bs;
    }
    if (max_bs == 0)
        max_bs = defaultBlockSize;

    if (max_bs != 64)
        warn_once("Blocksize found to not be 64... hmm... probably not.\n");
    cachedBlockSize = max_bs;
    cachedBlockSizeValid = true;
    return max_bs;
}


unsigned int
Bus::drain(Event * de)
{
    //We should check that we're not "doing" anything, and that noone is
    //waiting. We might be idle but have someone waiting if the device we
    //contacted for a retry didn't actually retry.
    if (retryList.size() || (curTick < tickNextIdle && busIdle.scheduled())) {
        drainEvent = de;
        return 1;
    }
    return 0;
}

void
Bus::startup()
{
    if (tickNextIdle < curTick)
        tickNextIdle = (curTick / clock) * clock + clock;
}

Bus *
BusParams::create()
{
    return new Bus(this);
}
