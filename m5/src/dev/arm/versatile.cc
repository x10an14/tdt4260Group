/*
 * Copyright (c) 2009 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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
 * Authors: Ali Saidi
 */

/** @file
 * Implementation of Versatile platform.
 */

#include <deque>
#include <string>
#include <vector>

#include "config/the_isa.hh"
#include "cpu/intr_control.hh"
#include "dev/arm/versatile.hh"
#include "dev/terminal.hh"
#include "sim/system.hh"

using namespace std;
using namespace TheISA;

Versatile::Versatile(const Params *p)
    : Platform(p), system(p->system)
{
    // set the back pointer from the system to myself
    system->platform = this;
}

Tick
Versatile::intrFrequency()
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

void
Versatile::postConsoleInt()
{
    warn_once("Don't know what interrupt to post for console.\n");
    //panic("Need implementation\n");
}

void
Versatile::clearConsoleInt()
{
    warn_once("Don't know what interrupt to clear for console.\n");
    //panic("Need implementation\n");
}

void
Versatile::postPciInt(int line)
{
    panic("Need implementation\n");
}

void
Versatile::clearPciInt(int line)
{
    panic("Need implementation\n");
}

Addr
Versatile::pciToDma(Addr pciAddr) const
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}


Addr
Versatile::calcPciConfigAddr(int bus, int dev, int func)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

Addr
Versatile::calcPciIOAddr(Addr addr)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

Addr
Versatile::calcPciMemAddr(Addr addr)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

Versatile *
VersatileParams::create()
{
    return new Versatile(this);
}
