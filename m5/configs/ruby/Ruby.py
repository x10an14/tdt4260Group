# Copyright (c) 2006-2007 The Regents of The University of Michigan
# Copyright (c) 2009 Advanced Micro Devices, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Brad Beckmann

import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath

def create_system(options, physmem, piobus = None, dma_devices = []):

    protocol = buildEnv['PROTOCOL']
    exec "import %s" % protocol
    try:
        (cpu_sequencers, dir_cntrls, all_cntrls) = \
          eval("%s.create_system(options, physmem, piobus, dma_devices)" \
               % protocol)
    except:
        print "Error: could not create sytem for ruby protocol %s" % protocol
        sys.exit(1)
        
    #
    # Important: the topology must be created before the network and after the
    # controllers.
    #
    exec "import %s" % options.topology
    try:
        net_topology = eval("%s.makeTopology(all_cntrls, options)" % options.topology)
    except:
        print "Error: could not create topology %s" % options.topology
        sys.exit(1)
        
    if options.garnet_network == "fixed":
        network = GarnetNetwork_d(topology = net_topology)
    elif options.garnet_network == "flexible":
        network = GarnetNetwork(topology = net_topology)
    else:
        network = SimpleNetwork(topology = net_topology)

    #
    # Determine the total memory size of the ruby system and verify it is equal
    # to physmem.  However, if Ruby memory is using sparse memory in SE 
    # mode, then the system should not back-up the memory state with
    # the Memory Vector and thus the memory size bytes should stay at 0.
    #
    total_mem_size = MemorySize('0B')
    for dir_cntrl in dir_cntrls:
        total_mem_size.value += dir_cntrl.directory.size.value
    physmem_size = long(physmem.range.second) - long(physmem.range.first) + 1
    assert(total_mem_size.value == physmem_size)

    ruby_profiler = RubyProfiler(num_of_sequencers = len(cpu_sequencers))
    
    ruby = RubySystem(clock = options.clock,
                      network = network,
                      profiler = ruby_profiler,
                      tracer = RubyTracer(),
                      debug = RubyDebug(filter_string = 'none',
                                        verbosity_string = 'none',
                                        protocol_trace = False),
                      mem_size = total_mem_size)

    ruby.cpu_ruby_ports = cpu_sequencers

    return ruby
