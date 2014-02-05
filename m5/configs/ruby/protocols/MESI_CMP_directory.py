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

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath

#
# Note: the L1 Cache latency is only used by the sequencer on fast path hits
#
class L1Cache(RubyCache):
    latency = 3

#
# Note: the L2 Cache latency is not currently used
#
class L2Cache(RubyCache):
    latency = 15

def create_system(options, phys_mem, piobus, dma_devices):
    
    if buildEnv['PROTOCOL'] != 'MESI_CMP_directory':
        panic("This script requires the MESI_CMP_directory protocol to be built.")

    cpu_sequencers = []
    
    #
    # The ruby network creation expects the list of nodes in the system to be
    # consistent with the NetDest list.  Therefore the l1 controller nodes must be
    # listed before the directory nodes and directory nodes before dma nodes, etc.
    #
    l1_cntrl_nodes = []
    l2_cntrl_nodes = []
    dir_cntrl_nodes = []
    dma_cntrl_nodes = []

    #
    # Must create the individual controllers before the network to ensure the
    # controller constructors are called before the network constructor
    #
    
    for i in xrange(options.num_cpus):
        #
        # First create the Ruby objects associated with this cpu
        #
        l1i_cache = L1Cache(size = options.l1i_size,
                            assoc = options.l1i_assoc)
        l1d_cache = L1Cache(size = options.l1d_size,
                            assoc = options.l1d_assoc)

        cpu_seq = RubySequencer(version = i,
                                icache = l1i_cache,
                                dcache = l1d_cache,
                                physMemPort = phys_mem.port,
                                physmem = phys_mem)

        if piobus != None:
            cpu_seq.pio_port = piobus.port

        l1_cntrl = L1Cache_Controller(version = i,
                                      sequencer = cpu_seq,
                                      L1IcacheMemory = l1i_cache,
                                      L1DcacheMemory = l1d_cache,
                                      l2_select_num_bits = \
                                        math.log(options.num_l2caches, 2))
        #
        # Add controllers and sequencers to the appropriate lists
        #
        cpu_sequencers.append(cpu_seq)
        l1_cntrl_nodes.append(l1_cntrl)

    for i in xrange(options.num_l2caches):
        #
        # First create the Ruby objects associated with this cpu
        #
        l2_cache = L2Cache(size = options.l2_size,
                           assoc = options.l2_assoc)

        l2_cntrl = L2Cache_Controller(version = i,
                                      L2cacheMemory = l2_cache)
        
        l2_cntrl_nodes.append(l2_cntrl)
        
    phys_mem_size = long(phys_mem.range.second) - long(phys_mem.range.first) + 1
    mem_module_size = phys_mem_size / options.num_dirs

    for i in xrange(options.num_dirs):
        #
        # Create the Ruby objects associated with the directory controller
        #

        mem_cntrl = RubyMemoryControl(version = i)

        dir_size = MemorySize('0B')
        dir_size.value = mem_module_size

        dir_cntrl = Directory_Controller(version = i,
                                         directory = \
                                         RubyDirectoryMemory(version = i,
                                                             size = dir_size),
                                         memBuffer = mem_cntrl)

        dir_cntrl_nodes.append(dir_cntrl)

    for i, dma_device in enumerate(dma_devices):
        #
        # Create the Ruby objects associated with the dma controller
        #
        dma_seq = DMASequencer(version = i,
                               physMemPort = phys_mem.port,
                               physmem = phys_mem)
        
        dma_cntrl = DMA_Controller(version = i,
                                   dma_sequencer = dma_seq)

        dma_cntrl.dma_sequencer.port = dma_device.dma
        dma_cntrl_nodes.append(dma_cntrl)

    all_cntrls = l1_cntrl_nodes + \
                 l2_cntrl_nodes + \
                 dir_cntrl_nodes + \
                 dma_cntrl_nodes

    return (cpu_sequencers, dir_cntrl_nodes, all_cntrls)
