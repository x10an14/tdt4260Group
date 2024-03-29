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

#include "base/str.hh"
#include "config/the_isa.hh"
#include "cpu/inorder/pipeline_stage.hh"
#include "cpu/inorder/resource_pool.hh"
#include "cpu/inorder/cpu.hh"

using namespace std;
using namespace ThePipeline;

PipelineStage::PipelineStage(Params *params, unsigned stage_num)
    : stageNum(stage_num), stageWidth(ThePipeline::StageWidth),
      numThreads(ThePipeline::MaxThreads), _status(Inactive),
      stageBufferMax(ThePipeline::interStageBuffSize[stage_num]),
      prevStageValid(false), nextStageValid(false), idle(false)
{
    switchedOutBuffer.resize(ThePipeline::MaxThreads);
    switchedOutValid.resize(ThePipeline::MaxThreads);
    
    init(params);
}

void
PipelineStage::init(Params *params)
{
    for(ThreadID tid = 0; tid < numThreads; tid++) {
        stageStatus[tid] = Idle;

        for (int stNum = 0; stNum < NumStages; stNum++) {
            stalls[tid].stage[stNum] = false;
        }
        stalls[tid].resources.clear();

        if (stageNum < BackEndStartStage)
            lastStallingStage[tid] = BackEndStartStage - 1;
        else
            lastStallingStage[tid] = NumStages - 1;
    }
}


std::string
PipelineStage::name() const
{
     return cpu->name() + ".stage-" + to_string(stageNum);
}


void
PipelineStage::regStats()
{
   idleCycles
        .name(name() + ".idleCycles")
       .desc("Number of cycles 0 instructions are processed.");
   
    runCycles
        .name(name() + ".runCycles")
        .desc("Number of cycles 1+ instructions are processed.");

    utilization
        .name(name() + ".utilization")
        .desc("Percentage of cycles stage was utilized (processing insts).")
        .precision(6);
    utilization = (runCycles / cpu->numCycles) * 100;
    
}


void
PipelineStage::setCPU(InOrderCPU *cpu_ptr)
{
    cpu = cpu_ptr;

    DPRINTF(InOrderStage, "Set CPU pointer.\n");

    tracer = dynamic_cast<Trace::InOrderTrace *>(cpu->getTracer());
}


void
PipelineStage::setTimeBuffer(TimeBuffer<TimeStruct> *tb_ptr)
{
    DPRINTF(InOrderStage, "Setting time buffer pointer.\n");
    timeBuffer = tb_ptr;

    // Setup wire to write information back to fetch.
    toPrevStages = timeBuffer->getWire(0);

    // Create wires to get information from proper places in time buffer.
    fromNextStages = timeBuffer->getWire(-1);
}


void
PipelineStage::setPrevStageQueue(TimeBuffer<InterStageStruct> *prev_stage_ptr)
{
    DPRINTF(InOrderStage, "Setting previous stage queue pointer.\n");
    prevStageQueue = prev_stage_ptr;

    // Setup wire to read information from fetch queue.
    prevStage = prevStageQueue->getWire(-1);

    prevStageValid = true;
}



void
PipelineStage::setNextStageQueue(TimeBuffer<InterStageStruct> *next_stage_ptr)
{
    DPRINTF(InOrderStage, "Setting next stage pointer.\n");
    nextStageQueue = next_stage_ptr;

    // Setup wire to write information to proper place in stage queue.
    nextStage = nextStageQueue->getWire(0);
    nextStage->size = 0;
    nextStageValid = true;
}



void
PipelineStage::setActiveThreads(list<ThreadID> *at_ptr)
{
    DPRINTF(InOrderStage, "Setting active threads list pointer.\n");
    activeThreads = at_ptr;
}

/*inline void
PipelineStage::switchToActive()
{
   if (_status == Inactive) {
        DPRINTF(Activity, "Activating stage.\n");

        cpu->activateStage(stageNum);

        _status = Active;
    }
}*/

void
PipelineStage::switchOut()
{
    // Stage can immediately switch out.
    panic("Switching Out of Stages Unimplemented");
}


void
PipelineStage::takeOverFrom()
{
    _status = Inactive;

    // Be sure to reset state and clear out any old instructions.
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        stageStatus[tid] = Idle;

        for (int stNum = 0; stNum < NumStages; stNum++) {
            stalls[tid].stage[stNum] = false;
        }

        stalls[tid].resources.clear();

        while (!insts[tid].empty())
            insts[tid].pop();

        while (!skidBuffer[tid].empty())
            skidBuffer[tid].pop();
    }
    wroteToTimeBuffer = false;
}



bool
PipelineStage::checkStall(ThreadID tid) const
{
    bool ret_val = false;

    // Only check pipeline stall from stage directly following this stage
    if (nextStageValid && stalls[tid].stage[stageNum + 1]) {
        DPRINTF(InOrderStage,"[tid:%i]: Stall fom Stage %i detected.\n",
                tid, stageNum + 1);
        ret_val = true;
    }

    if (!stalls[tid].resources.empty()) {
        string stall_src;

        for (int i=0; i < stalls[tid].resources.size(); i++) {
            stall_src += stalls[tid].resources[i]->res->name() + ":";
        }

        DPRINTF(InOrderStage,"[tid:%i]: Stall fom resources (%s) detected.\n",
                tid, stall_src);
        ret_val = true;
    }

    return ret_val;
}


void
PipelineStage::removeStalls(ThreadID tid)
{
    for (int st_num = 0; st_num < NumStages; st_num++) {
        if (stalls[tid].stage[st_num] == true) {
            DPRINTF(InOrderStage, "Removing stall from stage %i.\n",
                    st_num);
            stalls[tid].stage[st_num] = false;
        }

        if (toPrevStages->stageBlock[st_num][tid] == true) {
            DPRINTF(InOrderStage, "Removing pending block from stage %i.\n",
                    st_num);
            toPrevStages->stageBlock[st_num][tid] = false;
        }

        if (fromNextStages->stageBlock[st_num][tid] == true) {
            DPRINTF(InOrderStage, "Removing pending block from stage %i.\n",
                    st_num);
            fromNextStages->stageBlock[st_num][tid] = false;
        }
    }
    stalls[tid].resources.clear();
}

inline bool
PipelineStage::prevStageInstsValid()
{
    return prevStage->size > 0;
}

bool
PipelineStage::isBlocked(ThreadID tid)
{
    return stageStatus[tid] == Blocked;
}

bool
PipelineStage::block(ThreadID tid)
{
    DPRINTF(InOrderStage, "[tid:%d]: Blocking, sending block signal back to "
            "previous stages.\n", tid);

    // Add the current inputs to the skid buffer so they can be
    // reprocessed when this stage unblocks.
    // skidInsert(tid);

    // If the stage status is blocked or unblocking then stage has not yet
    // signalled fetch to unblock. In that case, there is no need to tell
    // fetch to block.
    if (stageStatus[tid] != Blocked) {
        // Set the status to Blocked.
        stageStatus[tid] = Blocked;

        if (stageStatus[tid] != Unblocking) {
            if (prevStageValid)
                toPrevStages->stageBlock[stageNum][tid] = true;
            wroteToTimeBuffer = true;
        }

        return true;
    }


    return false;
}

void
PipelineStage::blockDueToBuffer(ThreadID tid)
{
    DPRINTF(InOrderStage, "[tid:%d]: Blocking instructions from passing to "
            "next stage.\n", tid);

    if (stageStatus[tid] != Blocked) {
        // Set the status to Blocked.
        stageStatus[tid] = Blocked;

        if (stageStatus[tid] != Unblocking) {
            wroteToTimeBuffer = true;
        }
    }
}

bool
PipelineStage::unblock(ThreadID tid)
{
    // Stage is done unblocking only if the skid buffer is empty.
    if (skidBuffer[tid].empty()) {
        DPRINTF(InOrderStage, "[tid:%u]: Done unblocking.\n", tid);

        if (prevStageValid)
            toPrevStages->stageUnblock[stageNum][tid] = true;

        wroteToTimeBuffer = true;

        stageStatus[tid] = Running;

        return true;
    }

    DPRINTF(InOrderStage, "[tid:%u]: Currently unblocking.\n", tid);
    return false;
}

void
PipelineStage::squashDueToBranch(DynInstPtr &inst, ThreadID tid)
{
    if (cpu->squashSeqNum[tid] < inst->seqNum &&
        cpu->lastSquashCycle[tid] == curTick){
        DPRINTF(Resource, "Ignoring [sn:%i] branch squash signal due to "
                "another stage's squash signal for after [sn:%i].\n", 
                inst->seqNum, cpu->squashSeqNum[tid]);
    } else {
        // Send back mispredict information.
        toPrevStages->stageInfo[stageNum][tid].branchMispredict = true;
        toPrevStages->stageInfo[stageNum][tid].predIncorrect = true;
        toPrevStages->stageInfo[stageNum][tid].doneSeqNum = inst->seqNum;
        toPrevStages->stageInfo[stageNum][tid].squash = true;
        toPrevStages->stageInfo[stageNum][tid].nextPC = inst->readPredTarg();


#if ISA_HAS_DELAY_SLOT
        toPrevStages->stageInfo[stageNum][tid].branchTaken = 
            inst->readNextNPC() !=
            (inst->readNextPC() + sizeof(TheISA::MachInst));

        toPrevStages->stageInfo[stageNum][tid].bdelayDoneSeqNum = 
            inst->bdelaySeqNum;

        InstSeqNum squash_seq_num = inst->bdelaySeqNum;
#else
        toPrevStages->stageInfo[stageNum][tid].branchTaken = 
            inst->readNextPC() !=
            (inst->readPC() + sizeof(TheISA::MachInst));

        toPrevStages->stageInfo[stageNum][tid].bdelayDoneSeqNum = inst->seqNum;
        InstSeqNum squash_seq_num = inst->seqNum;
#endif

        DPRINTF(InOrderStage, "Target being re-set to %08p\n", 
                inst->readPredTarg());
        DPRINTF(InOrderStage, "[tid:%i]: Squashing after [sn:%i], "
                "due to [sn:%i] branch.\n", tid, squash_seq_num, 
                inst->seqNum);

        // Save squash num for later stage use
        cpu->squashSeqNum[tid] = squash_seq_num;
        cpu->lastSquashCycle[tid] = curTick;
    }
}

void
PipelineStage::squashDueToMemStall(InstSeqNum seq_num, ThreadID tid)
{
    squash(seq_num, tid);    
}

void
PipelineStage::squashPrevStageInsts(InstSeqNum squash_seq_num, ThreadID tid)
{
    DPRINTF(InOrderStage, "[tid:%i]: Removing instructions from "
            "incoming stage queue.\n", tid);

    for (int i=0; i < prevStage->size; i++) {
        if (prevStage->insts[i]->threadNumber == tid &&
            prevStage->insts[i]->seqNum > squash_seq_num) {
            // Change Comment to Annulling previous instruction
            DPRINTF(InOrderStage, "[tid:%i]: Squashing instruction, "
                    "[sn:%i] PC %08p.\n",
                    tid,
                    prevStage->insts[i]->seqNum,
                    prevStage->insts[i]->readPC());
            prevStage->insts[i]->setSquashed();

            prevStage->insts[i] = cpu->dummyBufferInst;
        }
    }
}

void
PipelineStage::squash(InstSeqNum squash_seq_num, ThreadID tid)
{
    // Set status to squashing.
    stageStatus[tid] = Squashing;

    squashPrevStageInsts(squash_seq_num, tid);

    DPRINTF(InOrderStage, "[tid:%i]: Removing instructions from incoming stage"
            " skidbuffer.\n", tid);
    while (!skidBuffer[tid].empty()) {
        if (skidBuffer[tid].front()->seqNum <= squash_seq_num) {
            DPRINTF(InOrderStage, "[tid:%i]: Cannot remove skidBuffer "
                    "instructions (starting w/[sn:%i]) before delay slot "
                    "[sn:%i]. %i insts left.\n", tid, 
                    skidBuffer[tid].front()->seqNum, squash_seq_num,
                    skidBuffer[tid].size());
            break;
        }
        DPRINTF(InOrderStage, "[tid:%i]: Removing instruction, [sn:%i] "
                " PC %08p.\n", tid, skidBuffer[tid].front()->seqNum, 
                skidBuffer[tid].front()->PC);
        skidBuffer[tid].pop();
    }

}

int
PipelineStage::stageBufferAvail()
{
    unsigned total = 0;

    for (int i=0; i < ThePipeline::MaxThreads; i++) {
        total += skidBuffer[i].size();
    }

    int incoming_insts = (prevStageValid) ?
        cpu->pipelineStage[stageNum]->prevStage->size :
        0;

    int avail = stageBufferMax - total -0;// incoming_insts;

    if (avail < 0)
        fatal("stageNum %i:stageBufferAvail() < 0..."
              "stBMax=%i,total=%i,incoming=%i=>%i",
              stageNum, stageBufferMax, total, incoming_insts, avail);

    return avail;
}

bool
PipelineStage::canSendInstToStage(unsigned stage_num)
{
    bool buffer_avail = false;

    if (cpu->pipelineStage[stage_num]->prevStageValid) {
        buffer_avail = cpu->pipelineStage[stage_num]->stageBufferAvail() >= 1;
    }

    if (!buffer_avail && nextStageQueueValid(stage_num)) {
        DPRINTF(InOrderStall, "STALL: No room in stage %i buffer.\n", 
                stageNum + 1);
    }

    return buffer_avail;
}

void
PipelineStage::skidInsert(ThreadID tid)
{
    DynInstPtr inst = NULL;

    while (!insts[tid].empty()) {
        inst = insts[tid].front();

        insts[tid].pop();

        assert(tid == inst->threadNumber);

        DPRINTF(InOrderStage,"[tid:%i]: Inserting [sn:%lli] PC:%#x into stage "
                "skidBuffer %i\n", tid, inst->seqNum, inst->readPC(), 
                inst->threadNumber);

        skidBuffer[tid].push(inst);
    }
}


int
PipelineStage::skidSize()
{
    int total = 0;

    for (int i=0; i < ThePipeline::MaxThreads; i++) {
        total += skidBuffer[i].size();
    }

    return total;
}

bool
PipelineStage::skidsEmpty()
{
    list<ThreadID>::iterator threads = activeThreads->begin();

    while (threads != activeThreads->end()) {
        if (!skidBuffer[*threads++].empty())
            return false;
    }

    return true;
}



void
PipelineStage::updateStatus()
{
    bool any_unblocking = false;

    list<ThreadID>::iterator threads = activeThreads->begin();

    while (threads != activeThreads->end()) {
        ThreadID tid = *threads++;

        if (stageStatus[tid] == Unblocking) {
            any_unblocking = true;
            break;
        }
    }

    // Stage will have activity if it's unblocking.
    if (any_unblocking) {
        if (_status == Inactive) {
            _status = Active;

            DPRINTF(Activity, "Activating stage.\n");

            cpu->activateStage(stageNum);
        }
    } else {
        // If it's not unblocking, then stage will not have any internal
        // activity.  Switch it to inactive.
        if (_status == Active) {
            _status = Inactive;
            DPRINTF(Activity, "Deactivating stage.\n");

            cpu->deactivateStage(stageNum);
        }
    }
}

void 
PipelineStage::activateThread(ThreadID tid)
{    
    if (cpu->threadModel == InOrderCPU::SwitchOnCacheMiss) {
        if (!switchedOutValid[tid]) {
            DPRINTF(InOrderStage, "[tid:%i] No instruction available in "
                    "switch out buffer.\n", tid);        
        } else {
            DynInstPtr inst = switchedOutBuffer[tid];

            DPRINTF(InOrderStage,"[tid:%i]: Re-Inserting [sn:%lli] PC:%#x into"
                    " stage skidBuffer %i\n", tid, inst->seqNum,
                    inst->readPC(), inst->threadNumber);

            // Make instruction available for pipeline processing
            skidBuffer[tid].push(inst);            

            // Update PC so that we start fetching after this instruction to
            // prevent "double"-execution of instructions
            cpu->resPool->scheduleEvent(InOrderCPU::UpdateAfterContextSwitch,
                                        inst, 0, 0, tid);

            // Clear switchout buffer
            switchedOutBuffer[tid] = NULL;
            switchedOutValid[tid] = false;            

            // Update any CPU stats based off context switches
            cpu->updateContextSwitchStats();            
        }        
    }
    
}


void
PipelineStage::sortInsts()
{
    if (prevStageValid) {
        int insts_from_prev_stage = prevStage->size;

        DPRINTF(InOrderStage, "%i insts available from stage buffer %i.\n",
                insts_from_prev_stage, prevStageQueue->id());

        for (int i = 0; i < insts_from_prev_stage; ++i) {

            if (prevStage->insts[i]->isSquashed()) {
                DPRINTF(InOrderStage, "[tid:%i]: Ignoring squashed [sn:%i], "
                        "not inserting into stage buffer.\n",
                    prevStage->insts[i]->readTid(),
                    prevStage->insts[i]->seqNum);

                continue;
            }

            DPRINTF(InOrderStage, "[tid:%i]: Inserting [sn:%i] into stage "
                    "buffer.\n", prevStage->insts[i]->readTid(),
                    prevStage->insts[i]->seqNum);

            ThreadID tid = prevStage->insts[i]->threadNumber;

            DynInstPtr inst = prevStage->insts[i];

            skidBuffer[tid].push(prevStage->insts[i]);

            prevStage->insts[i] = cpu->dummyBufferInst;

        }
    }
}



void
PipelineStage::readStallSignals(ThreadID tid)
{
    for (int stage_idx = stageNum+1; stage_idx <= lastStallingStage[tid];
         stage_idx++) {

        // Check for Stage Blocking Signal
        if (fromNextStages->stageBlock[stage_idx][tid]) {
            DPRINTF(InOrderStage, "[tid:%i] Stall from stage %i set.\n", tid,
                    stage_idx);
            stalls[tid].stage[stage_idx] = true;
        }

        // Check for Stage Unblocking Signal
        if (fromNextStages->stageUnblock[stage_idx][tid]) {
            DPRINTF(InOrderStage, "[tid:%i] Stall from stage %i unset.\n", tid,
                    stage_idx);
            stalls[tid].stage[stage_idx] = false;
        }
    }
}



bool
PipelineStage::checkSignalsAndUpdate(ThreadID tid)
{
    // Check if there's a squash signal, squash if there is.
    // Check stall signals, block if necessary.
    // If status was blocked
    //     Check if stall conditions have passed
    //         if so then go to unblocking
    // If status was Squashing
    //     check if squashing is not high.  Switch to running this cycle.

    // Update the per thread stall statuses.
    readStallSignals(tid);

    // Check for squash from later pipeline stages
    for (int stage_idx=stageNum; stage_idx < NumStages; stage_idx++) {
        if (fromNextStages->stageInfo[stage_idx][tid].squash) {
            DPRINTF(InOrderStage, "[tid:%u]: Squashing instructions due to "
                    "squash from stage %u.\n", tid, stage_idx);
            InstSeqNum squash_seq_num = fromNextStages->
                stageInfo[stage_idx][tid].bdelayDoneSeqNum;
            squash(squash_seq_num, tid);
            break; //return true;
        }
    }

    if (checkStall(tid)) {
        return block(tid);
    }

    if (stageStatus[tid] == Blocked) {
        DPRINTF(InOrderStage, "[tid:%u]: Done blocking, switching to "
                "unblocking.\n", tid);

        stageStatus[tid] = Unblocking;

        unblock(tid);

        return true;
    }

    if (stageStatus[tid] == Squashing) {
        if (!skidBuffer[tid].empty()) {
            DPRINTF(InOrderStage, "[tid:%u]: Done squashing, switching to "
                    "unblocking.\n", tid);

            stageStatus[tid] = Unblocking;
        } else {
            // Switch status to running if stage isn't being told to block or
            // squash this cycle.
            DPRINTF(InOrderStage, "[tid:%u]: Done squashing, switching to "
                    "running.\n", tid);

            stageStatus[tid] = Running;
        }

        return true;
    }

    // If we've reached this point, we have not gotten any signals that
    // cause stage to change its status.  Stage remains the same as before.*/
    return false;
}



void
PipelineStage::tick()
{
    idle = false;
    
    wroteToTimeBuffer = false;

    bool status_change = false;

    if (nextStageValid)
        nextStage->size = 0;

    toNextStageIndex = 0;
    
    sortInsts();

    instsProcessed = 0;

    processStage(status_change);

    if (status_change) {
        updateStatus();
    }

    if (wroteToTimeBuffer) {
        DPRINTF(Activity, "Activity this cycle.\n");
        cpu->activityThisCycle();
    }

    DPRINTF(InOrderStage, "\n\n");
}

void
PipelineStage::setResStall(ResReqPtr res_req, ThreadID tid)
{
    DPRINTF(InOrderStage, "Inserting stall from %s.\n", res_req->res->name());
    stalls[tid].resources.push_back(res_req);
}

void
PipelineStage::unsetResStall(ResReqPtr res_req, ThreadID tid)
{
    // Search through stalls to find stalling request and then
    // remove it
    vector<ResReqPtr>::iterator req_it = stalls[tid].resources.begin();
    vector<ResReqPtr>::iterator req_end = stalls[tid].resources.end();

    while (req_it != req_end) {
        if( (*req_it)->res ==  res_req->res && // Same Resource
            (*req_it)->inst ==  res_req->inst && // Same Instruction
            (*req_it)->getSlot() ==  res_req->getSlot()) {
            DPRINTF(InOrderStage, "[tid:%u]: Clearing stall by %s.\n",
                    tid, res_req->res->name());
            stalls[tid].resources.erase(req_it);
            break;
        }

        req_it++;
    }

    if (stalls[tid].resources.size() == 0) {
        DPRINTF(InOrderStage, "[tid:%u]: There are no remaining resource"
                "stalls.\n", tid);
    }
}

// @TODO: Update How we handled threads in CPU. Maybe threads shouldnt be 
// handled one at a time, but instead first come first serve by instruction?
// Questions are how should a pipeline stage handle thread-specific stalls &
// pipeline squashes
void
PipelineStage::processStage(bool &status_change)
{
    list<ThreadID>::iterator threads = activeThreads->begin();

    //Check stall and squash signals.
    while (threads != activeThreads->end()) {
        ThreadID tid = *threads++;

        DPRINTF(InOrderStage,"Processing [tid:%i]\n",tid);
        status_change =  checkSignalsAndUpdate(tid) || status_change;

        processThread(status_change, tid);
    }

    if (nextStageValid) {
        DPRINTF(InOrderStage, "%i insts now available for stage %i.\n",
                nextStage->size, stageNum + 1);
    }

    if (instsProcessed > 0) {
        ++runCycles;
        idle = false;        
    } else {
        ++idleCycles;        
        idle = true;        
    }
    
    DPRINTF(InOrderStage, "%i left in stage %i incoming buffer.\n", skidSize(),
            stageNum);

    DPRINTF(InOrderStage, "%i available in stage %i incoming buffer.\n", 
            stageBufferAvail(), stageNum);
}

void
PipelineStage::processThread(bool &status_change, ThreadID tid)
{
    // If status is Running or idle,
    //     call processInsts()
    // If status is Unblocking,
    //     buffer any instructions coming from fetch
   //     continue trying to empty skid buffer
    //     check if stall conditions have passed

    // Stage should try to process as many instructions as its bandwidth
    // will allow, as long as it is not currently blocked.
    if (stageStatus[tid] == Running ||
        stageStatus[tid] == Idle) {
        DPRINTF(InOrderStage, "[tid:%u]: Not blocked, so attempting to run "
                "stage.\n",tid);

        processInsts(tid);
    } else if (stageStatus[tid] == Unblocking) {
        // Make sure that the skid buffer has something in it if the
        // status is unblocking.
        assert(!skidsEmpty());

        // If the status was unblocking, then instructions from the skid
        // buffer were used.  Remove those instructions and handle
        // the rest of unblocking.
        processInsts(tid);

        if (prevStageValid && prevStageInstsValid()) {
            // Add the current inputs to the skid buffer so they can be
            // reprocessed when this stage unblocks.
            skidInsert(tid);
        }

        status_change = unblock(tid) || status_change;
    }
}


void
PipelineStage::processInsts(ThreadID tid)
{
    // Instructions can come either from the skid buffer or the list of
    // instructions coming from fetch, depending on stage's status.
    int insts_available = skidBuffer[tid].size();

    std::queue<DynInstPtr> &insts_to_stage = skidBuffer[tid];

    if (insts_available == 0) {
        DPRINTF(InOrderStage, "[tid:%u]: Nothing to do, breaking out"
                " early.\n",tid);
        return;
    }

    DynInstPtr inst;
    bool last_req_completed = true;

    while (insts_available > 0 &&
           instsProcessed < stageWidth &&
           (!nextStageValid || canSendInstToStage(stageNum+1)) &&
           last_req_completed) {
        assert(!insts_to_stage.empty());

        inst = insts_to_stage.front();

        DPRINTF(InOrderStage, "[tid:%u]: Processing instruction [sn:%lli] "
                "with PC %#x\n",
                tid, inst->seqNum, inst->readPC());

        if (inst->isSquashed()) {
            DPRINTF(InOrderStage, "[tid:%u]: Instruction %i with PC %#x is "
                    "squashed, skipping.\n",
                    tid, inst->seqNum, inst->readPC());

            insts_to_stage.pop();

            --insts_available;

            continue;
        }

        int reqs_processed = 0;        
        last_req_completed = processInstSchedule(inst, reqs_processed);

        // If the instruction isnt squashed & we've completed one request
        // Then we can officially count this instruction toward the stage's 
        // bandwidth count
        if (reqs_processed > 0)
            instsProcessed++;

        // Don't let instruction pass to next stage if it hasnt completed
        // all of it's requests for this stage.
        if (!last_req_completed)
            continue;

        // Send to Next Stage or Break Loop
        if (nextStageValid && !sendInstToNextStage(inst)) {
            DPRINTF(InOrderStage, "[tid:%i] [sn:%i] unable to proceed to stage"
                    " %i.\n", tid, inst->seqNum,inst->nextStage);
            break;
        }

        insts_to_stage.pop();

        --insts_available;
    }

    // If we didn't process all instructions, then we will need to block
    // and put all those instructions into the skid buffer.
    if (!insts_to_stage.empty()) {
        blockDueToBuffer(tid);
    }

    // Record that stage has written to the time buffer for activity
    // tracking.
    if (toNextStageIndex) {
        wroteToTimeBuffer = true;
    }
}

bool
PipelineStage::processInstSchedule(DynInstPtr inst,int &reqs_processed)
{
    bool last_req_completed = true;
    ThreadID tid = inst->readTid();

    if (inst->nextResStage() == stageNum) {
        int res_stage_num = inst->nextResStage();

        while (res_stage_num == stageNum) {
            int res_num = inst->nextResource();


            DPRINTF(InOrderStage, "[tid:%i]: [sn:%i]: sending request to %s."
                    "\n", tid, inst->seqNum, cpu->resPool->name(res_num));

            ResReqPtr req = cpu->resPool->request(res_num, inst);

            if (req->isCompleted()) {
                DPRINTF(InOrderStage, "[tid:%i]: [sn:%i] request to %s "
                        "completed.\n", tid, inst->seqNum, 
                        cpu->resPool->name(res_num));

                if (req->fault == NoFault) {
                    inst->popSchedEntry();
                } else {
                    panic("%i: encountered %s fault!\n",
                          curTick, req->fault->name());
                }

                reqs_processed++;                

                req->stagePasses++;                
            } else {
                DPRINTF(InOrderStage, "[tid:%i]: [sn:%i] request to %s failed."
                        "\n", tid, inst->seqNum, cpu->resPool->name(res_num));

                last_req_completed = false;

                if (req->isMemStall() && 
                    cpu->threadModel == InOrderCPU::SwitchOnCacheMiss) {
                    // Save Stalling Instruction
                    DPRINTF(ThreadModel, "[tid:%i] [sn:%i] Detected cache "
                            "miss.\n", tid, inst->seqNum);

                    DPRINTF(InOrderStage, "Inserting [tid:%i][sn:%i] into "
                            "switch out buffer.\n", tid, inst->seqNum);

                    switchedOutBuffer[tid] = inst;
                    switchedOutValid[tid] = true;
                    
                    // Remove Thread From Pipeline & Resource Pool
                    inst->squashingStage = stageNum;         
                    inst->bdelaySeqNum = inst->seqNum;                               
                    cpu->squashFromMemStall(inst, tid);  

                    // Switch On Cache Miss
                    //=====================
                    // Suspend Thread at end of cycle
                    DPRINTF(ThreadModel, "Suspending [tid:%i] due to cache "
                            "miss.\n", tid);
                    cpu->suspendContext(tid);                    

                    // Activate Next Ready Thread at end of cycle
                    DPRINTF(ThreadModel, "Attempting to activate next ready "
                            "thread due to cache miss.\n");
                    cpu->activateNextReadyContext();                                                                                               
                }
                
                // Mark request for deletion
                // if it isnt currently being used by a resource
                if (!req->hasSlot()) {                   
                    DPRINTF(InOrderStage, "[sn:%i] Deleting Request, has no "
                            "slot in resource.\n", inst->seqNum);
                    
                    cpu->reqRemoveList.push(req);
                } else {
                    DPRINTF(InOrderStage, "[sn:%i] Ignoring Request Deletion, "
                            "in resource [slot:%i].\n", inst->seqNum,
                            req->getSlot());
                }
                
                
                break;
            }

            res_stage_num = inst->nextResStage();
        }
    } else {
        DPRINTF(InOrderStage, "[tid:%u]: Instruction [sn:%i] with PC %#x "
                " needed no resources in stage %i.\n",
                tid, inst->seqNum, inst->readPC(), stageNum);
    }

    return last_req_completed;
}

bool
PipelineStage::nextStageQueueValid(int stage_num)
{
    return cpu->pipelineStage[stage_num]->nextStageValid;
}


bool
PipelineStage::sendInstToNextStage(DynInstPtr inst)
{
    // Update Next Stage Variable in Instruction
    // NOTE: Some Resources will update this nextStage var. to
    // for bypassing, so can't always assume nextStage=stageNum+1
    if (inst->nextStage == stageNum)
        inst->nextStage++;

    bool success = false;
    ThreadID tid = inst->readTid();
    int next_stage = inst->nextStage;
    int prev_stage = next_stage - 1;

    assert(next_stage >= 1);
    assert(prev_stage >= 0);

    DPRINTF(InOrderStage, "[tid:%u]: Attempting to send instructions to "
            "stage %u.\n", tid, stageNum+1);

    if (!canSendInstToStage(inst->nextStage)) {
        DPRINTF(InOrderStage, "[tid:%u]: Could not send instruction to "
                "stage %u.\n", tid, stageNum+1);
        return false;
    }


    if (nextStageQueueValid(inst->nextStage - 1)) {
        if (inst->seqNum > cpu->squashSeqNum[tid] &&
            curTick == cpu->lastSquashCycle[tid]) {
            DPRINTF(InOrderStage, "[tid:%u]: [sn:%i]: squashed, skipping "
                    "insertion into stage %i queue.\n", tid, inst->seqNum, 
                    inst->nextStage);
        } else {
            if (nextStageValid) {
                DPRINTF(InOrderStage, "[tid:%u] %i slots available in next "
                        "stage buffer.\n", tid, 
                        cpu->pipelineStage[next_stage]->stageBufferAvail());
            }

            DPRINTF(InOrderStage, "[tid:%u]: [sn:%i]: being placed into  "
                    "index %i of stage buffer %i queue.\n",
                    tid, inst->seqNum, toNextStageIndex,
                    cpu->pipelineStage[prev_stage]->nextStageQueue->id());

            int next_stage_idx = 
                cpu->pipelineStage[prev_stage]->nextStage->size;

            // Place instructions in inter-stage communication struct for next
            // pipeline stage to read next cycle
            cpu->pipelineStage[prev_stage]->nextStage->insts[next_stage_idx] 
                = inst;

            ++(cpu->pipelineStage[prev_stage]->nextStage->size);

            ++toNextStageIndex;

            success = true;

            // Take note of trace data for this inst & stage
            if (inst->traceData) {
                inst->traceData->setStageCycle(stageNum, curTick);
            }

        }
    }

    return success;
}

void
PipelineStage::dumpInsts()
{
    cprintf("Insts in Stage %i skidbuffers\n",stageNum);

    for (ThreadID tid = 0; tid < ThePipeline::MaxThreads; tid++) {
        std::queue<DynInstPtr> copy_buff(skidBuffer[tid]);

        while (!copy_buff.empty()) {
            DynInstPtr inst = copy_buff.front();

            cprintf("Inst. PC:%#x\n[tid:%i]\n[sn:%i]\n\n",
                    inst->readPC(), inst->threadNumber, inst->seqNum);

            copy_buff.pop();
        }
    }

}
