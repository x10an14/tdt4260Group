# Copyright (c) 2006-2008 The Regents of The University of Michigan
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
# Authors: Steve Reinhardt

# Simple test script
#
# "m5 test.py"

import os
import optparse
import sys
from os.path import join as joinpath

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, panic

if buildEnv['FULL_SYSTEM']:
    panic("This script requires syscall emulation mode (*_SE).")

addToPath('../common')
addToPath('../ruby')

import Ruby

import Simulation
from cpu2000 import *

# Get paths we might need.  It's expected this file is in m5/configs/example.
config_path = os.path.dirname(os.path.abspath(__file__))
config_root = os.path.dirname(config_path)
m5_root = os.path.dirname(config_root)

parser = optparse.OptionParser()

# Benchmark options
parser.add_option("-c", "--cmd",
    default=joinpath(m5_root, "tests/test-progs/hello/bin/alpha/linux/hello"),
    help="The binary to run in syscall emulation mode.")
parser.add_option("-o", "--options", default="",
    help='The options to pass to the binary, use " " around the entire string')
parser.add_option("-i", "--input", default="", help="Read stdin from a file.")
parser.add_option("--output", default="", help="Redirect stdout to a file.")
parser.add_option("--errout", default="", help="Redirect stderr to a file.")

# cache parameters
parser.add_option("--l1d_size", type="string", default="32kB")
parser.add_option("--l1i_size", type="string", default="32kB")
parser.add_option("--l2_size", type="string", default="1MB")
parser.add_option("--l1d_assoc", type="int", default=2)
parser.add_option("--l1i_assoc", type="int", default=2)
parser.add_option("--l2_assoc", type="int", default=16)
 
execfile(os.path.join(config_root, "common", "Options.py"))

(options, args) = parser.parse_args()

if args:
    print "Error: script doesn't take any positional arguments"
    sys.exit(1)

if options.bench:
    try:
        if buildEnv['TARGET_ISA'] != 'alpha':
            print >>sys.stderr, "Simpoints code only works for Alpha ISA at this time"
            sys.exit(1)
        exec("workload = %s('alpha', 'tru64', 'ref')" % options.bench)
        process = workload.makeLiveProcess()
    except:
        print >>sys.stderr, "Unable to find workload for %s" % options.bench
        sys.exit(1)
else:
    process = LiveProcess()
    process.executable = options.cmd
    process.cmd = [options.cmd] + options.options.split()


if options.input != "":
    process.input = options.input
if options.output != "":
    process.output = options.output
if options.errout != "":
    process.errout = options.errout

if options.detailed:
    #check for SMT workload
    workloads = options.cmd.split(';')
    if len(workloads) > 1:
        process = []
        smt_idx = 0
        inputs = []
        outputs = []
        errouts = []

        if options.input != "":
            inputs = options.input.split(';')
        if options.output != "":
            outputs = options.output.split(';')
        if options.errout != "":
            errouts = options.errout.split(';')

        for wrkld in workloads:
            smt_process = LiveProcess()
            smt_process.executable = wrkld
            smt_process.cmd = wrkld + " " + options.options
            if inputs and inputs[smt_idx]:
                smt_process.input = inputs[smt_idx]
            if outputs and outputs[smt_idx]:
                smt_process.output = outputs[smt_idx]
            if errouts and errouts[smt_idx]:
                smt_process.errout = errouts[smt_idx]
            process += [smt_process, ]
            smt_idx += 1

(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(options)

#
# Currently ruby only works in timing mode 
#
assert(options.timing)
assert(test_mem_mode == 'timing')
assert(FutureClass == None)

CPUClass.clock = '1GHz'

np = options.num_cpus

system = System(cpu = [CPUClass(cpu_id=i) for i in xrange(np)],
                physmem = PhysicalMemory())

system.ruby = Ruby.create_system(options, system.physmem)

assert(options.num_cpus == len(system.ruby.cpu_ruby_ports))

for (i, cpu) in enumerate(system.cpu):
    #
    # Tie the cpu ports to the ruby cpu ports
    #
    cpu.icache_port = system.ruby.cpu_ruby_ports[i].port
    cpu.dcache_port = system.ruby.cpu_ruby_ports[i].port

    '''process = LiveProcess()
    process.executable = options.cmd
    process.cmd = [options.cmd, str(i)]
    '''
    cpu.workload = process

root = Root(system = system)

Simulation.run(options, root, system, FutureClass)
