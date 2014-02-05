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
 * Unordered buffer of messages that can be inserted such
 * that they can be dequeued after a given delta time has expired.
 */

#ifndef __MEM_RUBY_BUFFERS_MESSAGEBUFFER_HH__
#define __MEM_RUBY_BUFFERS_MESSAGEBUFFER_HH__

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>
#include <string>

#include "mem/ruby/buffers/MessageBufferNode.hh"
#include "mem/ruby/common/Consumer.hh"
#include "mem/ruby/common/Global.hh"
#include "mem/ruby/eventqueue/RubyEventQueue.hh"
#include "mem/ruby/slicc_interface/Message.hh"

class MessageBuffer
{
  public:
    MessageBuffer(const std::string &name = "");

    static void printConfig(std::ostream& out) {}
    void
    setRecycleLatency(int recycle_latency)
    {
        m_recycle_latency = recycle_latency;
    }

    // TRUE if head of queue timestamp <= SystemTime
    bool
    isReady() const
    {
        return ((m_prio_heap.size() > 0) &&
                (m_prio_heap.front().m_time <= g_eventQueue_ptr->getTime()));
    }

    void
    delayHead()
    {
        MessageBufferNode node = m_prio_heap.front();
        std::pop_heap(m_prio_heap.begin(), m_prio_heap.end(),
                      std::greater<MessageBufferNode>());
        m_prio_heap.pop_back();
        enqueue(node.m_msgptr, 1);
    }

    bool areNSlotsAvailable(int n);
    int getPriority() { return m_priority_rank; }
    void setPriority(int rank) { m_priority_rank = rank; }
    void setConsumer(Consumer* consumer_ptr)
    {
        ASSERT(m_consumer_ptr == NULL);
        m_consumer_ptr = consumer_ptr;
    }

    void setDescription(const std::string& name) { m_name = name; }
    std::string getDescription() { return m_name;}

    Consumer* getConsumer() { return m_consumer_ptr; }

    const Message* peekAtHeadOfQueue() const;
    const Message* peek() const { return peekAtHeadOfQueue(); }
    const MsgPtr getMsgPtrCopy() const;

    const MsgPtr&
    peekMsgPtr() const
    {
        assert(isReady());
        return m_prio_heap.front().m_msgptr;
    }

    const MsgPtr&
    peekMsgPtrEvenIfNotReady() const
    {
        return m_prio_heap.front().m_msgptr;
    }

    void enqueue(MsgPtr message) { enqueue(message, 1); }
    void enqueue(MsgPtr message, Time delta);
    //  void enqueueAbsolute(const MsgPtr& message, Time absolute_time);
    int dequeue_getDelayCycles(MsgPtr& message);  // returns delay
                                                  // cycles of the
                                                  // message
    void dequeue(MsgPtr& message);
    int dequeue_getDelayCycles();  // returns delay cycles of the message
    void dequeue() { pop(); }
    void pop();
    void recycle();
    bool isEmpty() const { return m_prio_heap.size() == 0; }

    void
    setOrdering(bool order)
    {
        m_strict_fifo = order;
        m_ordering_set = true;
    }
    void resize(int size) { m_max_size = size; }
    int getSize();
    void setRandomization(bool random_flag) { m_randomization = random_flag; }

    void clear();

    void print(std::ostream& out) const;
    void printStats(std::ostream& out);
    void clearStats() { m_not_avail_count = 0; m_msg_counter = 0; }

  private:
    //added by SS
    int m_recycle_latency;

    // Private Methods
    int setAndReturnDelayCycles(MsgPtr message);

    // Private copy constructor and assignment operator
    MessageBuffer(const MessageBuffer& obj);
    MessageBuffer& operator=(const MessageBuffer& obj);

    // Data Members (m_ prefix)
    Consumer* m_consumer_ptr;  // Consumer to signal a wakeup(), can be NULL
    std::vector<MessageBufferNode> m_prio_heap;
    std::string m_name;

    int m_max_size;
    int m_size;

    Time m_time_last_time_size_checked;
    int m_size_last_time_size_checked;

    // variables used so enqueues appear to happen imediately, while
    // pop happen the next cycle
    Time m_time_last_time_enqueue;
    Time m_time_last_time_pop;
    int m_size_at_cycle_start;
    int m_msgs_this_cycle;

    int m_not_avail_count;  // count the # of times I didn't have N
                            // slots available
    int m_msg_counter;
    int m_priority_rank;
    bool m_strict_fifo;
    bool m_ordering_set;
    bool m_randomization;
    Time m_last_arrival_time;
};

inline std::ostream&
operator<<(std::ostream& out, const MessageBuffer& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

#endif // __MEM_RUBY_BUFFERS_MESSAGEBUFFER_HH__
