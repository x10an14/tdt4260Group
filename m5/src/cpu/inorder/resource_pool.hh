/*
 * Copyright (c) 2007 MIPS Technologies, Inc.
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
 * Authors: Korey Sewell
 *
 */

#ifndef __CPU_INORDER_RESOURCE_POOL_HH__
#define __CPU_INORDER_RESOURCE_POOL_HH__

#include <vector>
#include <list>
#include <string>

#include "cpu/inst_seq.hh"
#include "cpu/inorder/inorder_dyn_inst.hh"
#include "cpu/inorder/resource.hh"
#include "cpu/inorder/pipeline_traits.hh"
#include "cpu/inorder/params.hh"
#include "params/InOrderCPU.hh"
#include "cpu/inorder/cpu.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

class Event;
class InOrderCPU;
class Resource;
class ResourceEvent;

class ResourcePool {
  public:
    typedef InOrderDynInst::DynInstPtr DynInstPtr;

  public:
    // List of Resource Pool Events that extends
    // the list started by the CPU
    // NOTE(1): Resource Pool also uses event list
    // CPUEventType defined in inorder/cpu.hh

    class ResPoolEvent : public Event
    {
      protected:
        /** Resource Pool */
        ResourcePool *resPool;

      public:
        InOrderCPU::CPUEventType eventType;

        DynInstPtr inst;

        InstSeqNum seqNum;

        int stageNum;

        ThreadID tid;

      public:
        /** Constructs a resource event. */
        ResPoolEvent(ResourcePool *_resPool,
                     InOrderCPU::CPUEventType e_type,
                     DynInstPtr _inst,
                     int stage_num,
                     InstSeqNum seq_num,
                     ThreadID _tid);

        /** Set Type of Event To Be Scheduled */
        void setEvent(InOrderCPU::CPUEventType e_type,
                      DynInstPtr _inst,
                      int stage_num,
                      InstSeqNum seq_num,
                      ThreadID _tid)
        {
            eventType = e_type;
            inst = _inst;
            seqNum = seq_num;
            stageNum = stage_num;
            tid = _tid;
        }

        /** Processes a resource event. */
        void process();

        /** Returns the description of the resource event. */
        const char *description();

        /** Schedule Event */
        void scheduleEvent(int delay);

        /** Unschedule This Event */
        void unscheduleEvent();
    };

  public:
    ResourcePool(InOrderCPU *_cpu, ThePipeline::Params *params);
    virtual ~ResourcePool();    

    std::string name();

    std::string name(int res_idx) { return resources[res_idx]->name(); }

    void init();

    /** Register Statistics in All Resources */
    void regStats();

    /** Returns a specific port. */
    Port* getPort(const std::string &if_name, int idx);

    /** Returns a specific port. */
    unsigned getPortIdx(const std::string &port_name);

    /** Returns a specific resource. */
    unsigned getResIdx(const std::string &res_name);
    unsigned getResIdx(const ThePipeline::ResourceId &res_id);

    /** Returns a pointer to a resource */
    Resource* getResource(int res_idx) { return resources[res_idx]; }

    /** Request usage of this resource. Returns -1 if not granted and
     *  a positive request tag if granted.
     */
    ResReqPtr request(int res_idx, DynInstPtr inst);

    /** Squash The Resource */
    void squash(DynInstPtr inst, int res_idx, InstSeqNum done_seq_num,
                ThreadID tid);

    /** Squash All Resources in Pool after Done Seq. Num */
    void squashAll(DynInstPtr inst, int stage_num,
                   InstSeqNum done_seq_num, ThreadID tid);

    /** Squash Resources in Pool after a memory stall 
     *  NOTE: Only use during Switch-On-Miss Thread model
     */    
    void squashDueToMemStall(DynInstPtr inst, int stage_num,
                             InstSeqNum done_seq_num, ThreadID tid);

    /** Activate Thread in all resources */
    void activateAll(ThreadID tid);

    /** De-Activate Thread in all resources */
    void deactivateAll(ThreadID tid);

    /** De-Activate Thread in all resources */
    void suspendAll(ThreadID tid);

    /** Broadcast Context Switch Update to all resources */
    void updateAfterContextSwitch(DynInstPtr inst, ThreadID tid);

    /** Broadcast graduation to all resources */
    void instGraduated(InstSeqNum seq_num, ThreadID tid);

    /** The number of instructions available that a resource can
     *  can still process.
     */
    int slotsAvail(int res_idx);

    /** The number of instructions using a resource */
    int slotsInUse(int res_idx);

    /** Schedule resource event, regardless of its current state. */
    void scheduleEvent(InOrderCPU::CPUEventType e_type, DynInstPtr inst = NULL,
                       int delay = 0, int res_idx = 0, ThreadID tid = 0);

   /** UnSchedule resource event, regardless of its current state. */
    void unscheduleEvent(int res_idx, DynInstPtr inst);

    /** Tasks to perform when simulation starts */
    virtual void startup() { }

    /** The CPU(s) that this resource interacts with */
    InOrderCPU *cpu;

    DynInstPtr dummyInst[ThePipeline::MaxThreads];

  private:
    std::vector<Resource *> resources;

    std::vector<int> memObjects;

};

#endif //__CPU_INORDER_RESOURCE_HH__
