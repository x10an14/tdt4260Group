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

#include "config/the_isa.hh"
#include "cpu/inorder/resources/decode_unit.hh"

using namespace TheISA;
using namespace ThePipeline;
using namespace std;

DecodeUnit::DecodeUnit(std::string res_name, int res_id, int res_width,
                       int res_latency, InOrderCPU *_cpu,
                       ThePipeline::Params *params)
    : Resource(res_name, res_id, res_width, res_latency, _cpu)
{
    for (ThreadID tid = 0; tid < MaxThreads; tid++) {
        regDepMap[tid] = &cpu->archRegDepMap[tid];
    }
}

void
DecodeUnit::execute(int slot_num)
{
    ResourceRequest* decode_req = reqMap[slot_num];
    DynInstPtr inst = reqMap[slot_num]->inst;
    Fault fault = reqMap[slot_num]->fault;
    ThreadID tid = inst->readTid();

    decode_req->fault = NoFault;

    switch (decode_req->cmd)
    {
      case DecodeInst:
        {
            bool done_sked = ThePipeline::createBackEndSchedule(inst);

            if (done_sked) {
                DPRINTF(InOrderDecode,
                    "[tid:%i]: Setting Destination Register(s) for [sn:%i].\n",
                    tid, inst->seqNum);
                regDepMap[tid]->insert(inst);
                decode_req->done();
            } else {
                DPRINTF(Resource,
                    "[tid:%i] Static Inst not available to decode.\n", tid);
                DPRINTF(Resource,
                    "Unable to create schedule for instruction [sn:%i] \n",
                    inst->seqNum);
                DPRINTF(InOrderStall, "STALL: \n");
                decode_req->done(false);
            }
        }
        break;

      default:
        fatal("Unrecognized command to %s", resName);
    }
}


void
DecodeUnit::squash(DynInstPtr inst, int stage_num, InstSeqNum squash_seq_num,
                   ThreadID tid)
{
    DPRINTF(InOrderDecode,
            "[tid:%i]: Updating due to squash from stage %i after [sn:%i].\n",
            tid, stage_num, squash_seq_num);

    //cpu->removeInstsUntil(squash_seq_num, tid);
}
