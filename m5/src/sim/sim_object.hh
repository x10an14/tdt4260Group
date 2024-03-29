/*
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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
 * Authors: Steve Reinhardt
 *          Nathan Binkert
 */

/* @file
 * User Console Definitions
 */

#ifndef __SIM_OBJECT_HH__
#define __SIM_OBJECT_HH__

#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "params/SimObject.hh"
#include "sim/eventq.hh"
#include "sim/serialize.hh"

class BaseCPU;
class Event;

/*
 * Abstract superclass for simulation objects.  Represents things that
 * correspond to physical components and can be specified via the
 * config file (CPUs, caches, etc.).
 */
class SimObject : public EventManager, public Serializable
{
  public:
    enum State {
        Running,
        Draining,
        Drained
    };

  private:
    State state;

  protected:
    void changeState(State new_state) { state = new_state; }

  public:
    State getState() { return state; }

  private:
    typedef std::vector<SimObject *> SimObjectList;

    // list of all instantiated simulation objects
    static SimObjectList simObjectList;

  protected:
    const SimObjectParams *_params;

  public:
    typedef SimObjectParams Params;
    const Params *params() const { return _params; }
    SimObject(const Params *_params);
    virtual ~SimObject() {}

  public:

    virtual const std::string name() const { return params()->name; }

    // initialization pass of all objects.
    // Gets invoked after construction, before unserialize.
    virtual void init();
    static void initAll();

    // register statistics for this object
    virtual void regStats();
    virtual void regFormulas();
    virtual void resetStats();

    // final initialization before simulation
    // all state is unserialized so 
    virtual void startup();
    static void startupAll();

    // static: call reg_stats on all SimObjects
    static void regAllStats();

    // static: call resetStats on all SimObjects
    static void resetAllStats();

    // static: call nameOut() & serialize() on all SimObjects
    static void serializeAll(std::ostream &);
    static void unserializeAll(Checkpoint *cp);

    // Methods to drain objects in order to take checkpoints
    // Or switch from timing -> atomic memory model
    // Drain returns 0 if the simobject can drain immediately or
    // the number of times the drain_event's process function will be called
    // before the object will be done draining. Normally this should be 1
    virtual unsigned int drain(Event *drain_event);
    virtual void resume();
    virtual void setMemoryMode(State new_mode);
    virtual void switchOut();
    virtual void takeOverFrom(BaseCPU *cpu);

#ifdef DEBUG
  public:
    bool doDebugBreak;
    static void debugObjectBreak(const std::string &objs);
#endif

    /**
     * Find the SimObject with the given name and return a pointer to
     * it.  Primarily used for interactive debugging.  Argument is
     * char* rather than std::string to make it callable from gdb.
     */
    static SimObject *find(const char *name);
};

#endif // __SIM_OBJECT_HH__
