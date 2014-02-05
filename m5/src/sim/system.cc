/*
 * Copyright (c) 2003-2006 The Regents of The University of Michigan
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
 *          Lisa Hsu
 *          Nathan Binkert
 *          Ali Saidi
 */

#include "arch/isa_traits.hh"
#include "arch/remote_gdb.hh"
#include "arch/utility.hh"
#include "base/loader/object_file.hh"
#include "base/loader/symtab.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "config/full_system.hh"
#include "config/the_isa.hh"
#include "mem/mem_object.hh"
#include "mem/physical.hh"
#include "sim/byteswap.hh"
#include "sim/system.hh"
#include "sim/debug.hh"

#if FULL_SYSTEM
#include "arch/vtophys.hh"
#include "kern/kernel_stats.hh"
#else
#include "params/System.hh"
#endif

using namespace std;
using namespace TheISA;

vector<System *> System::systemList;

int System::numSystemsRunning = 0;

System::System(Params *p)
    : SimObject(p), physmem(p->physmem), _numContexts(0),
#if FULL_SYSTEM
      init_param(p->init_param),
      functionalPort(p->name + "-fport"),
      virtPort(p->name + "-vport"),
#else
      page_ptr(0),
      next_PID(0),
#endif
      memoryMode(p->mem_mode), _params(p)
{
    // add self to global system list
    systemList.push_back(this);

#if FULL_SYSTEM
    kernelSymtab = new SymbolTable;
    if (!debugSymbolTable)
        debugSymbolTable = new SymbolTable;


    /**
     * Get a functional port to memory
     */
    Port *mem_port;
    mem_port = physmem->getPort("functional");
    functionalPort.setPeer(mem_port);
    mem_port->setPeer(&functionalPort);

    mem_port = physmem->getPort("functional");
    virtPort.setPeer(mem_port);
    mem_port->setPeer(&virtPort);


    /**
     * Load the kernel code into memory
     */
    if (params()->kernel == "") {
        inform("No kernel set for full system simulation. Assuming you know what"
                " you're doing...\n");
    } else {
        // Load kernel code
        kernel = createObjectFile(params()->kernel);
        inform("kernel located at: %s", params()->kernel);

        if (kernel == NULL)
            fatal("Could not load kernel file %s", params()->kernel);

        // Load program sections into memory
        kernel->loadSections(&functionalPort, LoadAddrMask);

        // setup entry points
        kernelStart = kernel->textBase();
        kernelEnd = kernel->bssBase() + kernel->bssSize();
        kernelEntry = kernel->entryPoint();

        // load symbols
        if (!kernel->loadGlobalSymbols(kernelSymtab))
            fatal("could not load kernel symbols\n");

        if (!kernel->loadLocalSymbols(kernelSymtab))
            fatal("could not load kernel local symbols\n");

        if (!kernel->loadGlobalSymbols(debugSymbolTable))
            fatal("could not load kernel symbols\n");

        if (!kernel->loadLocalSymbols(debugSymbolTable))
            fatal("could not load kernel local symbols\n");

        DPRINTF(Loader, "Kernel start = %#x\n", kernelStart);
        DPRINTF(Loader, "Kernel end   = %#x\n", kernelEnd);
        DPRINTF(Loader, "Kernel entry = %#x\n", kernelEntry);
        DPRINTF(Loader, "Kernel loaded...\n");
    }
#endif // FULL_SYSTEM

    // increment the number of running systms
    numSystemsRunning++;
}

System::~System()
{
#if FULL_SYSTEM
    delete kernelSymtab;
    delete kernel;
#else
    panic("System::fixFuncEventAddr needs to be rewritten "
          "to work with syscall emulation");
#endif // FULL_SYSTEM}
}

void
System::setMemoryMode(Enums::MemoryMode mode)
{
    assert(getState() == Drained);
    memoryMode = mode;
}

bool System::breakpoint()
{
    if (remoteGDB.size())
        return remoteGDB[0]->breakpoint();
    return false;
}

/**
 * Setting rgdb_wait to a positive integer waits for a remote debugger to
 * connect to that context ID before continuing.  This should really
   be a parameter on the CPU object or something...
 */
int rgdb_wait = -1;

int
System::registerThreadContext(ThreadContext *tc, int assigned)
{
    int id;
    if (assigned == -1) {
        for (id = 0; id < threadContexts.size(); id++) {
            if (!threadContexts[id])
                break;
        }

        if (threadContexts.size() <= id)
            threadContexts.resize(id + 1);
    } else {
        if (threadContexts.size() <= assigned)
            threadContexts.resize(assigned + 1);
        id = assigned;
    }

    if (threadContexts[id])
        fatal("Cannot have two CPUs with the same id (%d)\n", id);

    threadContexts[id] = tc;
    _numContexts++;

    int port = getRemoteGDBPort();
    if (port) {
        RemoteGDB *rgdb = new RemoteGDB(this, tc);
        GDBListener *gdbl = new GDBListener(rgdb, port + id);
        gdbl->listen();

        if (rgdb_wait != -1 && rgdb_wait == id)
            gdbl->accept();

        if (remoteGDB.size() <= id) {
            remoteGDB.resize(id + 1);
        }

        remoteGDB[id] = rgdb;
    }

    return id;
}

int
System::numRunningContexts()
{
    int running = 0;
    for (int i = 0; i < _numContexts; ++i) {
        if (threadContexts[i]->status() != ThreadContext::Halted)
            ++running;
    }
    return running;
}

void
System::startup()
{
#if FULL_SYSTEM
    int i;
    for (i = 0; i < threadContexts.size(); i++)
        TheISA::startupCPU(threadContexts[i], i);
#endif
}

void
System::replaceThreadContext(ThreadContext *tc, int context_id)
{
    if (context_id >= threadContexts.size()) {
        panic("replaceThreadContext: bad id, %d >= %d\n",
              context_id, threadContexts.size());
    }

    threadContexts[context_id] = tc;
    if (context_id < remoteGDB.size())
        remoteGDB[context_id]->replaceThreadContext(tc);
}

#if !FULL_SYSTEM
Addr
System::new_page()
{
    Addr return_addr = page_ptr << LogVMPageSize;
    ++page_ptr;
    if (return_addr >= physmem->size())
        fatal("Out of memory, please increase size of physical memory.");
    return return_addr;
}

Addr
System::memSize()
{
    return physmem->size();
}

Addr
System::freeMemSize()
{
   return physmem->size() - (page_ptr << LogVMPageSize);
}

#endif

void
System::serialize(ostream &os)
{
#if FULL_SYSTEM
    kernelSymtab->serialize("kernel_symtab", os);
#else // !FULL_SYSTEM
    SERIALIZE_SCALAR(page_ptr);
#endif
}


void
System::unserialize(Checkpoint *cp, const string &section)
{
#if FULL_SYSTEM
    kernelSymtab->unserialize("kernel_symtab", cp, section);
#else // !FULL_SYSTEM
    UNSERIALIZE_SCALAR(page_ptr);
#endif
}

void
System::printSystems()
{
    vector<System *>::iterator i = systemList.begin();
    vector<System *>::iterator end = systemList.end();
    for (; i != end; ++i) {
        System *sys = *i;
        cerr << "System " << sys->name() << ": " << hex << sys << endl;
    }
}

void
printSystems()
{
    System::printSystems();
}

const char *System::MemoryModeStrings[3] = {"invalid", "atomic",
    "timing"};

#if !FULL_SYSTEM

System *
SystemParams::create()
{
    return new System(this);
}

#endif
