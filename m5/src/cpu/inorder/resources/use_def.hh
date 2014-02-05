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

#ifndef __CPU_INORDER_USE_DEF_UNIT_HH__
#define __CPU_INORDER_USE_DEF_UNIT_HH__

#include <vector>
#include <list>
#include <string>

#include "cpu/func_unit.hh"
#include "cpu/inorder/first_stage.hh"
#include "cpu/inorder/resource.hh"
#include "cpu/inorder/inorder_dyn_inst.hh"
#include "cpu/inorder/pipeline_traits.hh"
#include "cpu/inorder/reg_dep_map.hh"

class UseDefUnit : public Resource {
  public:
    typedef ThePipeline::DynInstPtr DynInstPtr;

    enum Command {
        ReadSrcReg,
        WriteDestReg
    };

  public:
    UseDefUnit(std::string res_name, int res_id, int res_width,
               int res_latency, InOrderCPU *_cpu, ThePipeline::Params *params);

    ResourceRequest* getRequest(DynInstPtr _inst, int stage_num,
                                        int res_idx, int slot_num,
                                        unsigned cmd);

    ResReqPtr findRequest(DynInstPtr inst);

    void execute(int slot_num);

    void squash(DynInstPtr inst, int stage_num,
                        InstSeqNum squash_seq_num, ThreadID tid);

    void updateAfterContextSwitch(DynInstPtr inst, ThreadID tid);    

    const InstSeqNum maxSeqNum;

    void regStats();
    
  protected:
    RegDepMap *regDepMap[ThePipeline::MaxThreads];

    /** Outstanding Seq. Num. Trying to Read from Register File */
    InstSeqNum outReadSeqNum[ThePipeline::MaxThreads];

    InstSeqNum outWriteSeqNum[ThePipeline::MaxThreads];

    bool *nonSpecInstActive[ThePipeline::MaxThreads];

    InstSeqNum *nonSpecSeqNum[ThePipeline::MaxThreads];

    InstSeqNum floatRegSize[ThePipeline::MaxThreads];

    Stats::Average uniqueRegsPerSwitch;
    std::map<unsigned, bool> uniqueRegMap;    

  public:
    class UseDefRequest : public ResourceRequest {
      public:
        typedef ThePipeline::DynInstPtr DynInstPtr;

      public:
        UseDefRequest(UseDefUnit *res, DynInstPtr inst, int stage_num, 
                      int res_idx, int slot_num, unsigned cmd, 
                      int use_def_idx)
            : ResourceRequest(res, inst, stage_num, res_idx, slot_num, cmd),
              useDefIdx(use_def_idx)
        { }

        int useDefIdx;
    };

  protected:
    /** Register File Reads */
    Stats::Scalar regFileReads;

    /** Register File Writes */
    Stats::Scalar regFileWrites;

    /** Source Register Forwarding */
    Stats::Scalar regForwards;

    /** Register File Total Accesses (Read+Write) */
    Stats::Formula regFileAccs;
};

#endif //__CPU_INORDER_USE_DEF_UNIT_HH__
