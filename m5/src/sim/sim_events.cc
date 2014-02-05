/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 * Authors: Nathan Binkert
 */

#include <string>

#include "base/callback.hh"
#include "base/hostinfo.hh"
#include "sim/eventq.hh"
#include "sim/sim_events.hh"
#include "sim/sim_exit.hh"
#include "sim/stats.hh"

using namespace std;

SimLoopExitEvent::SimLoopExitEvent(const std::string &_cause, int c, Tick r)
    : Event(Sim_Exit_Pri), cause(_cause), code(c), repeat(r)
{
    setFlags(IsExitEvent);
}


//
// handle termination event
//
void
SimLoopExitEvent::process()
{
    // if this got scheduled on a different queue (e.g. the committed
    // instruction queue) then make a corresponding event on the main
    // queue.
    if (!getFlags(IsMainQueue)) {
        exitSimLoop(cause, code);

        // Drop exit event status so the event queue can delete this event.
        clearFlags(IsExitEvent);
        setFlags(AutoDelete);
    }

    // otherwise do nothing... the IsExitEvent flag takes care of
    // exiting the simulation loop and returning this object to Python

    // but if you are doing this on intervals, don't forget to make another
    if (repeat) {
        assert(getFlags(IsMainQueue));
        mainEventQueue.schedule(this, curTick + repeat);
    }
}


const char *
SimLoopExitEvent::description() const
{
    return "simulation loop exit";
}

void
exitSimLoop(const std::string &message, int exit_code)
{
    Event *event = new SimLoopExitEvent(message, exit_code);
    mainEventQueue.schedule(event, curTick);
}

CountedDrainEvent::CountedDrainEvent()
    : SimLoopExitEvent("Finished drain", 0), count(0)
{ }

void
CountedDrainEvent::process()
{
    if (--count == 0)
        exitSimLoop(cause, code);
}

//
// constructor: automatically schedules at specified time
//
CountedExitEvent::CountedExitEvent(const std::string &_cause, int &counter)
    : Event(Sim_Exit_Pri), cause(_cause), downCounter(counter)
{
    // catch stupid mistakes
    assert(downCounter > 0);
}


//
// handle termination event
//
void
CountedExitEvent::process()
{
    if (--downCounter == 0) {
        exitSimLoop(cause, 0);
    }
}


const char *
CountedExitEvent::description() const
{
    return "counted exit";
}

CheckSwapEvent::CheckSwapEvent(int ival)
    : interval(ival)
{
    mainEventQueue.schedule(this, curTick + interval);
}

void
CheckSwapEvent::process()
{
    /*  Check the amount of free swap space  */
    long swap;

    /*  returns free swap in KBytes  */
    swap = procInfo("/proc/meminfo", "SwapFree:");

    if (swap < 1000)
        ccprintf(cerr, "\a\a\aWarning! Swap space is low (%d)\n", swap);

    if (swap < 100) {
        cerr << "\a\aAborting Simulation! Inadequate swap space!\n\n";
        exitSimLoop("Lack of swap space");
    }

    assert(getFlags(IsMainQueue));
    mainEventQueue.schedule(this, curTick + interval);
}

const char *
CheckSwapEvent::description() const
{
    return "check swap";
}
