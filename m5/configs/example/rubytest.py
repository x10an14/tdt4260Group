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
# Authors: Ron Dreslinski
#          Brad Beckmann

import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath
import os, optparse, sys
addToPath('../common')
addToPath('../ruby')

import Ruby

if buildEnv['FULL_SYSTEM']:
    panic("This script requires system-emulation mode (*_SE).")

# Get paths we might need.  It's expected this file is in m5/configs/example.
config_path = os.path.dirname(os.path.abspath(__file__))
config_root = os.path.dirname(config_path)
m5_root = os.path.dirname(config_root)

parser = optparse.OptionParser()

parser.add_option("-l", "--checks", metavar="N", default=100,
                  help="Stop after N checks (loads)")
parser.add_option("-f", "--wakeup_freq", metavar="N", default=10,
                  help="Wakeup every N cycles")

#
# Set the default cache size and associativity to be very small to encourage
# races between requests and writebacks.
#
parser.add_option("--l1d_size", type="string", default="256B")
parser.add_option("--l1i_size", type="string", default="256B")
parser.add_option("--l2_size", type="string", default="512B")
parser.add_option("--l1d_assoc", type="int", default=2)
parser.add_option("--l1i_assoc", type="int", default=2)
parser.add_option("--l2_assoc", type="int", default=2)

execfile(os.path.join(config_root, "common", "Options.py"))

(options, args) = parser.parse_args()

if args:
     print "Error: script doesn't take any positional arguments"
     sys.exit(1)

#
# Create the ruby random tester
#
tester = RubyTester(checks_to_complete = options.checks,
                    wakeup_frequency = options.wakeup_freq)

#
# Create the M5 system.  Note that the PhysicalMemory Object isn't
# actually used by the rubytester, but is included to support the
# M5 memory size == Ruby memory size checks
#
system = System(physmem = PhysicalMemory())

system.ruby = Ruby.create_system(options, system.physmem)

assert(options.num_cpus == len(system.ruby.cpu_ruby_ports))

#
# The tester is most effective when randomization is turned on and
# artifical delay is randomly inserted on messages
#
system.ruby.randomization = True

for ruby_port in system.ruby.cpu_ruby_ports:
    #
    # Tie the ruby tester ports to the ruby cpu ports
    #
    tester.cpuPort = ruby_port.port

    #
    # Tell each sequencer this is the ruby tester so that it
    # copies the subblock back to the checker
    #
    ruby_port.using_ruby_tester = True

# -----------------------
# run simulation
# -----------------------

root = Root( system = system )
root.system.mem_mode = 'timing'

# Not much point in this being higher than the L1 latency
m5.ticks.setGlobalFrequency('1ns')

# instantiate configuration
m5.instantiate(root)

# simulate until program terminates
exit_event = m5.simulate(options.maxtick)

print 'Exiting @ tick', m5.curTick(), 'because', exit_event.getCause()
