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

#include "cpu/inorder/resources/agen_unit.hh"

AGENUnit::AGENUnit(std::string res_name, int res_id, int res_width,
                   int res_latency, InOrderCPU *_cpu,
                   ThePipeline::Params *params)
    : Resource(res_name, res_id, res_width, res_latency, _cpu)
{ }

void
AGENUnit::regStats()
{
    agens
        .name(name() + ".agens")
        .desc("Number of Address Generations");

    Resource::regStats();
}

void
AGENUnit::execute(int slot_num)
{
    ResourceRequest* agen_req = reqMap[slot_num];
    DynInstPtr inst = reqMap[slot_num]->inst;
    Fault fault = reqMap[slot_num]->fault;
#if TRACING_ON
    ThreadID tid = inst->readTid();
#endif
    int seq_num = inst->seqNum;

    agen_req->fault = NoFault;

    switch (agen_req->cmd)
    {
      case GenerateAddr:
        {
            // Load/Store Instruction
            if (inst->isMemRef()) {
                DPRINTF(InOrderAGEN,
                        "[tid:%i] Generating Address for [sn:%i] (%s).\n",
                        tid, seq_num, inst->staticInst->getName());

                fault = inst->calcEA();
                inst->setMemAddr(inst->getEA());

                DPRINTF(InOrderAGEN,
                    "[tid:%i] [sn:%i] Effective address calculated as: %#x\n",
                    tid, seq_num, inst->getEA());

                if (fault == NoFault) {
                    agen_req->done();
                } else {
                    fatal("%s encountered while calculating address [sn:%i]",
                          fault->name(), seq_num);
                }

                agens++;
            } else {
                DPRINTF(InOrderAGEN,
                        "[tid:] Ignoring non-memory instruction [sn:%i]\n",
                        tid, seq_num);
                agen_req->done();
            }
        }
        break;

      default:
        fatal("Unrecognized command to %s", resName);
    }
}
