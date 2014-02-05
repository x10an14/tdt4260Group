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

#ifndef __CPU_INORDER_FETCH_SEQ_UNIT_HH__
#define __CPU_INORDER_FETCH_SEQ_UNIT_HH__

#include <vector>
#include <list>
#include <string>

#include "config/the_isa.hh"
#include "cpu/inorder/resource.hh"
#include "cpu/inorder/inorder_dyn_inst.hh"
#include "cpu/inorder/pipeline_traits.hh"
#include "cpu/inorder/cpu.hh"

class FetchSeqUnit : public Resource {
  public:
    typedef ThePipeline::DynInstPtr DynInstPtr;

    enum Command {
        AssignNextPC,
        UpdateTargetPC
    };

  public:
    FetchSeqUnit(std::string res_name, int res_id, int res_width,
              int res_latency, InOrderCPU *_cpu, ThePipeline::Params *params);
    ~FetchSeqUnit();
    
    void init();
    void activateThread(ThreadID tid);
    void deactivateThread(ThreadID tid);
    void suspendThread(ThreadID tid);
    void execute(int slot_num);
    void updateAfterContextSwitch(DynInstPtr inst, ThreadID tid);
    

    /** Override default Resource squash sequence. This actually,
     *  looks in the global communication buffer to get squash
     *  info
     */
    void squash(DynInstPtr inst, int squash_stage,
                        InstSeqNum squash_seq_num, ThreadID tid);


    inline void squashAfterInst(DynInstPtr inst, int stage_num, ThreadID tid);

  protected:
    unsigned instSize;

    bool pcValid[ThePipeline::MaxThreads];
    int pcBlockStage[ThePipeline::MaxThreads];

    TheISA::IntReg PC[ThePipeline::MaxThreads];
    TheISA::IntReg nextPC[ThePipeline::MaxThreads];
    TheISA::IntReg nextNPC[ThePipeline::MaxThreads];

    /** Tracks delay slot information for threads in ISAs which use
     *	delay slots;
     */
    struct DelaySlotInfo {
        InstSeqNum delaySlotSeqNum;
        InstSeqNum branchSeqNum;
        int numInsts;
        Addr targetAddr;
        bool targetReady;
    };

    DelaySlotInfo delaySlotInfo[ThePipeline::MaxThreads];

    /** Squash Seq. Nums*/
    InstSeqNum squashSeqNum[ThePipeline::MaxThreads];

    /** Squash Seq. Nums*/
    Tick lastSquashCycle[ThePipeline::MaxThreads];

    /** @todo: Add Resource Stats Here */

  public:
    class FetchSeqEvent : public ResourceEvent {
      public:
        /** Constructs a resource event. */
        FetchSeqEvent();
        ~FetchSeqEvent() {}

        /** Processes a resource event. */
        void process();
    };

};

#endif
