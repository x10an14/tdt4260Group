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

#include "base/cprintf.hh"
#include "base/stl_helpers.hh"
#include "mem/ruby/buffers/MessageBuffer.hh"
#include "mem/ruby/system/System.hh"

using namespace std;
using m5::stl_helpers::operator<<;

MessageBuffer::MessageBuffer(const string &name)
{
    m_msg_counter = 0;
    m_consumer_ptr = NULL;
    m_ordering_set = false;
    m_strict_fifo = true;
    m_size = 0;
    m_max_size = -1;
    m_last_arrival_time = 0;
    m_randomization = true;
    m_size_last_time_size_checked = 0;
    m_time_last_time_size_checked = 0;
    m_time_last_time_enqueue = 0;
    m_time_last_time_pop = 0;
    m_size_at_cycle_start = 0;
    m_msgs_this_cycle = 0;
    m_not_avail_count = 0;
    m_priority_rank = 0;
    m_name = name;
}

int
MessageBuffer::getSize()
{
    if (m_time_last_time_size_checked == g_eventQueue_ptr->getTime()) {
        return m_size_last_time_size_checked;
    } else {
        m_time_last_time_size_checked = g_eventQueue_ptr->getTime();
        m_size_last_time_size_checked = m_size;
        return m_size;
    }
}

bool
MessageBuffer::areNSlotsAvailable(int n)
{

    // fast path when message buffers have infinite size
    if (m_max_size == -1) {
        return true;
    }

    // determine my correct size for the current cycle
    // pop operations shouldn't effect the network's visible size
    // until next cycle, but enqueue operations effect the visible
    // size immediately
    int current_size = max(m_size_at_cycle_start, m_size);
    if (m_time_last_time_pop < g_eventQueue_ptr->getTime()) {
        // no pops this cycle - m_size is correct
        current_size = m_size;
    } else {
        if (m_time_last_time_enqueue < g_eventQueue_ptr->getTime()) {
            // no enqueues this cycle - m_size_at_cycle_start is correct
            current_size = m_size_at_cycle_start;
        } else {
            // both pops and enqueues occured this cycle - add new
            // enqueued msgs to m_size_at_cycle_start
            current_size = m_size_at_cycle_start+m_msgs_this_cycle;
        }
    }

    // now compare the new size with our max size
    if (current_size + n <= m_max_size) {
        return true;
    } else {
        DEBUG_MSG(QUEUE_COMP, MedPrio, n);
        DEBUG_MSG(QUEUE_COMP, MedPrio, current_size);
        DEBUG_MSG(QUEUE_COMP, MedPrio, m_size);
        DEBUG_MSG(QUEUE_COMP, MedPrio, m_max_size);
        m_not_avail_count++;
        return false;
    }
}

const MsgPtr
MessageBuffer::getMsgPtrCopy() const
{
    assert(isReady());

    return m_prio_heap.front().m_msgptr->clone();
}

const Message*
MessageBuffer::peekAtHeadOfQueue() const
{
    DEBUG_NEWLINE(QUEUE_COMP, MedPrio);

    DEBUG_MSG(QUEUE_COMP, MedPrio,
              csprintf("Peeking at head of queue %s time: %d.",
                       m_name, g_eventQueue_ptr->getTime()));
    assert(isReady());

    const Message* msg_ptr = m_prio_heap.front().m_msgptr.get();
    assert(msg_ptr);

    DEBUG_EXPR(QUEUE_COMP, MedPrio, *msg_ptr);
    DEBUG_NEWLINE(QUEUE_COMP, MedPrio);
    return msg_ptr;
}

// FIXME - move me somewhere else
int
random_time()
{
    int time = 1;
    time += random() & 0x3;  // [0...3]
    if ((random() & 0x7) == 0) {  // 1 in 8 chance
        time += 100 + (random() % 0xf); // 100 + [1...15]
    }
    return time;
}

void
MessageBuffer::enqueue(MsgPtr message, Time delta)
{
    DEBUG_NEWLINE(QUEUE_COMP, HighPrio);
    DEBUG_MSG(QUEUE_COMP, HighPrio,
              csprintf("enqueue %s time: %d.", m_name,
                       g_eventQueue_ptr->getTime()));
    DEBUG_EXPR(QUEUE_COMP, MedPrio, message);
    DEBUG_NEWLINE(QUEUE_COMP, HighPrio);

    m_msg_counter++;
    m_size++;

    // record current time incase we have a pop that also adjusts my size
    if (m_time_last_time_enqueue < g_eventQueue_ptr->getTime()) {
        m_msgs_this_cycle = 0;  // first msg this cycle
        m_time_last_time_enqueue = g_eventQueue_ptr->getTime();
    }
    m_msgs_this_cycle++;

    //  ASSERT(m_max_size == -1 || m_size <= m_max_size + 1);
    // the plus one is a kluge because of a SLICC issue

    if (!m_ordering_set) {
        //    WARN_EXPR(*this);
        WARN_EXPR(m_name);
        ERROR_MSG("Ordering property of this queue has not been set");
    }

    // Calculate the arrival time of the message, that is, the first
    // cycle the message can be dequeued.
    //printf ("delta %i \n", delta);
    assert(delta>0);
    Time current_time = g_eventQueue_ptr->getTime();
    Time arrival_time = 0;
    if (!RubySystem::getRandomization() || (m_randomization == false)) {
        // No randomization
        arrival_time = current_time + delta;
    } else {
        // Randomization - ignore delta
        if (m_strict_fifo) {
            if (m_last_arrival_time < current_time) {
                m_last_arrival_time = current_time;
            }
            arrival_time = m_last_arrival_time + random_time();
        } else {
            arrival_time = current_time + random_time();
        }
    }

    // Check the arrival time
    assert(arrival_time > current_time);
    if (m_strict_fifo) {
        if (arrival_time < m_last_arrival_time) {
            WARN_EXPR(*this);
            WARN_EXPR(m_name);
            WARN_EXPR(current_time);
            WARN_EXPR(delta);
            WARN_EXPR(arrival_time);
            WARN_EXPR(m_last_arrival_time);
            ERROR_MSG("FIFO ordering violated");
        }
    }
    m_last_arrival_time = arrival_time;

    // compute the delay cycles and set enqueue time
    Message* msg_ptr = message.get();
    assert(msg_ptr != NULL);

    assert(g_eventQueue_ptr->getTime() >= msg_ptr->getLastEnqueueTime() &&
           "ensure we aren't dequeued early");

    msg_ptr->setDelayedCycles(g_eventQueue_ptr->getTime() -
                              msg_ptr->getLastEnqueueTime() +
                              msg_ptr->getDelayedCycles());
    msg_ptr->setLastEnqueueTime(arrival_time);

    // Insert the message into the priority heap
    MessageBufferNode thisNode(arrival_time, m_msg_counter, message);
    m_prio_heap.push_back(thisNode);
    push_heap(m_prio_heap.begin(), m_prio_heap.end(),
        greater<MessageBufferNode>());

    DEBUG_NEWLINE(QUEUE_COMP, HighPrio);
    DEBUG_MSG(QUEUE_COMP, HighPrio,
              csprintf("enqueue %s with arrival_time %d cur_time: %d.",
                       m_name, arrival_time, g_eventQueue_ptr->getTime()));
    DEBUG_EXPR(QUEUE_COMP, MedPrio, message);
    DEBUG_NEWLINE(QUEUE_COMP, HighPrio);

    // Schedule the wakeup
    if (m_consumer_ptr != NULL) {
        g_eventQueue_ptr->scheduleEventAbsolute(m_consumer_ptr, arrival_time);
    } else {
        WARN_EXPR(*this);
        WARN_EXPR(m_name);
        ERROR_MSG("No consumer");
    }
}

int
MessageBuffer::dequeue_getDelayCycles(MsgPtr& message)
{
    int delay_cycles = -1;  // null value

    dequeue(message);

    // get the delay cycles
    delay_cycles = setAndReturnDelayCycles(message);

    assert(delay_cycles >= 0);
    return delay_cycles;
}

void
MessageBuffer::dequeue(MsgPtr& message)
{
    DEBUG_MSG(QUEUE_COMP, MedPrio, "dequeue from " + m_name);
    message = m_prio_heap.front().m_msgptr;

    pop();
    DEBUG_EXPR(QUEUE_COMP, MedPrio, message);
}

int
MessageBuffer::dequeue_getDelayCycles()
{
    int delay_cycles = -1;  // null value

    // get MsgPtr of the message about to be dequeued
    MsgPtr message = m_prio_heap.front().m_msgptr;

    // get the delay cycles
    delay_cycles = setAndReturnDelayCycles(message);

    dequeue();

    assert(delay_cycles >= 0);
    return delay_cycles;
}

void
MessageBuffer::pop()
{
    DEBUG_MSG(QUEUE_COMP, MedPrio, "pop from " + m_name);
    assert(isReady());
    pop_heap(m_prio_heap.begin(), m_prio_heap.end(),
        greater<MessageBufferNode>());
    m_prio_heap.pop_back();

    // record previous size and time so the current buffer size isn't
    // adjusted until next cycle
    if (m_time_last_time_pop < g_eventQueue_ptr->getTime()) {
        m_size_at_cycle_start = m_size;
        m_time_last_time_pop = g_eventQueue_ptr->getTime();
    }
    m_size--;
}

void
MessageBuffer::clear()
{
    m_prio_heap.clear();

    m_msg_counter = 0;
    m_size = 0;
    m_time_last_time_enqueue = 0;
    m_time_last_time_pop = 0;
    m_size_at_cycle_start = 0;
    m_msgs_this_cycle = 0;
}

void
MessageBuffer::recycle()
{
    DEBUG_MSG(QUEUE_COMP, MedPrio, "recycling " + m_name);
    assert(isReady());
    MessageBufferNode node = m_prio_heap.front();
    pop_heap(m_prio_heap.begin(), m_prio_heap.end(),
        greater<MessageBufferNode>());
    node.m_time = g_eventQueue_ptr->getTime() + m_recycle_latency;
    m_prio_heap.back() = node;
    push_heap(m_prio_heap.begin(), m_prio_heap.end(),
        greater<MessageBufferNode>());
    g_eventQueue_ptr->scheduleEventAbsolute(m_consumer_ptr,
        g_eventQueue_ptr->getTime() + m_recycle_latency);
}

int
MessageBuffer::setAndReturnDelayCycles(MsgPtr msg_ptr)
{
    int delay_cycles = -1;  // null value

    // get the delay cycles of the message at the top of the queue

    // this function should only be called on dequeue
    // ensure the msg hasn't been enqueued
    assert(msg_ptr->getLastEnqueueTime() <= g_eventQueue_ptr->getTime());
    msg_ptr->setDelayedCycles(g_eventQueue_ptr->getTime() -
                              msg_ptr->getLastEnqueueTime() +
                              msg_ptr->getDelayedCycles());
    delay_cycles = msg_ptr->getDelayedCycles();

    assert(delay_cycles >= 0);
    return delay_cycles;
}

void
MessageBuffer::print(ostream& out) const
{
    out << "[MessageBuffer: ";
    if (m_consumer_ptr != NULL) {
        out << " consumer-yes ";
    }

    vector<MessageBufferNode> copy(m_prio_heap);
    sort_heap(copy.begin(), copy.end(), greater<MessageBufferNode>());
    out << copy << "] " << m_name << endl;
}

void
MessageBuffer::printStats(ostream& out)
{
    out << "MessageBuffer: " << m_name << " stats - msgs:" << m_msg_counter
        << " full:" << m_not_avail_count << endl;
}
