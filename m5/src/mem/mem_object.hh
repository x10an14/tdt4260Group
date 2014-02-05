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
 * Authors: Ron Dreslinski
 */

/**
 * @file
 * Base Memory Object declaration.
 */

#ifndef __MEM_MEM_OBJECT_HH__
#define __MEM_MEM_OBJECT_HH__

#include "mem/port.hh"
#include "params/MemObject.hh"
#include "sim/sim_object.hh"

/**
 * The base MemoryObject class, allows for an accesor function to a
 * simobj that returns the Port.
 */
class MemObject : public SimObject
{
  public:
    typedef MemObjectParams Params;
    const Params *params() const
    { return dynamic_cast<const Params *>(_params); }

    MemObject(const Params *params);

  public:
    /** Additional function to return the Port of a memory object. */
    virtual Port *getPort(const std::string &if_name, int idx = -1) = 0;

    /** Tell object that this port is about to disappear, so it should remove it
     * from any structures that it's keeping it in. */
    virtual void deletePortRefs(Port *p) ;
};

#endif //__MEM_MEM_OBJECT_HH__
