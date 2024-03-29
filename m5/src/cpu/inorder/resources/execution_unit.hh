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

#ifndef __CPU_INORDER_EXECUTION_UNIT_HH__
#define __CPU_INORDER_EXECUTION_UNIT_HH__

#include <vector>
#include <list>
#include <string>

#include "cpu/func_unit.hh"
#include "cpu/inorder/first_stage.hh"
#include "cpu/inorder/resource.hh"
#include "cpu/inorder/inorder_dyn_inst.hh"

class ExecutionUnit : public Resource {
  public:
    typedef ThePipeline::DynInstPtr DynInstPtr;

    enum Command {
        ExecuteInst
    };

  public:
    ExecutionUnit(std::string res_name, int res_id, int res_width,
              int res_latency, InOrderCPU *_cpu, ThePipeline::Params *params);

  public:
    void regStats();

    /** Execute the function of this resource. The Default is action
     *  is to do nothing. More specific models will derive from this
     *  class and define their own execute function.
     */
    void execute(int slot_num);

  protected:
    /////////////////////////////////////////////////////////////////
    //
    // RESOURCE STATISTICS
    //
    /////////////////////////////////////////////////////////////////
    Stats::Scalar predictedTakenIncorrect;
    Stats::Scalar predictedNotTakenIncorrect;
    Stats::Scalar predictedIncorrect;
    Stats::Scalar predictedCorrect;
    Stats::Formula mispredictPct;
    Stats::Scalar executions;
    Tick lastExecuteCycle;
};


#endif //__CPU_INORDER_EXCUTION_UNIT_HH__
