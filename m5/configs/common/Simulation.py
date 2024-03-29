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
# Authors: Lisa Hsu

from os import getcwd
from os.path import join as joinpath

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import *

addToPath('../common')

def setCPUClass(options):

    atomic = False
    if options.timing:
        class TmpClass(TimingSimpleCPU): pass
    elif options.detailed:
        if not options.caches:
            print "O3 CPU must be used with caches"
            sys.exit(1)
        class TmpClass(DerivO3CPU): pass
    elif options.inorder:
        if not options.caches:
            print "InOrder CPU must be used with caches"
            sys.exit(1)
        class TmpClass(InOrderCPU): pass
    else:
        class TmpClass(AtomicSimpleCPU): pass
        atomic = True

    CPUClass = None
    test_mem_mode = 'atomic'

    if not atomic:
        if options.checkpoint_restore != None or options.fast_forward:
            CPUClass = TmpClass
            class TmpClass(AtomicSimpleCPU): pass
        else:
            test_mem_mode = 'timing'

    return (TmpClass, test_mem_mode, CPUClass)


def run(options, root, testsys, cpu_class):
    if options.maxtick:
        maxtick = options.maxtick
    elif options.maxtime:
        simtime = m5.ticks.seconds(simtime)
        print "simulating for: ", simtime
        maxtick = simtime
    else:
        maxtick = m5.MaxTick

    if options.checkpoint_dir:
        cptdir = options.checkpoint_dir
    elif m5.options.outdir:
        cptdir = m5.options.outdir
    else:
        cptdir = getcwd()

    if options.fast_forward and options.checkpoint_restore != None:
        fatal("Can't specify both --fast-forward and --checkpoint-restore")

    if options.standard_switch and not options.caches:
        fatal("Must specify --caches when using --standard-switch")

    np = options.num_cpus
    max_checkpoints = options.max_checkpoints
    switch_cpus = None

    if options.prog_intvl:
        for i in xrange(np):
            testsys.cpu[i].progress_interval = options.prog_intvl

    if options.maxinsts:
        for i in xrange(np):
            testsys.cpu[i].max_insts_any_thread = options.maxinsts

    if cpu_class:
        switch_cpus = [cpu_class(defer_registration=True, cpu_id=(np+i))
                       for i in xrange(np)]

        for i in xrange(np):
            if options.fast_forward:
                testsys.cpu[i].max_insts_any_thread = int(options.fast_forward)
            switch_cpus[i].system =  testsys
            if not buildEnv['FULL_SYSTEM']:
                switch_cpus[i].workload = testsys.cpu[i].workload
            switch_cpus[i].clock = testsys.cpu[0].clock
            # simulation period
            if options.max_inst:
                switch_cpus[i].max_insts_any_thread = options.max_inst

        testsys.switch_cpus = switch_cpus
        switch_cpu_list = [(testsys.cpu[i], switch_cpus[i]) for i in xrange(np)]

    if options.standard_switch:
        switch_cpus = [TimingSimpleCPU(defer_registration=True, cpu_id=(np+i))
                       for i in xrange(np)]
        switch_cpus_1 = [DerivO3CPU(defer_registration=True, cpu_id=(2*np+i))
                        for i in xrange(np)]

        for i in xrange(np):
            switch_cpus[i].system =  testsys
            switch_cpus_1[i].system =  testsys
            if not buildEnv['FULL_SYSTEM']:
                switch_cpus[i].workload = testsys.cpu[i].workload
                switch_cpus_1[i].workload = testsys.cpu[i].workload
            switch_cpus[i].clock = testsys.cpu[0].clock
            switch_cpus_1[i].clock = testsys.cpu[0].clock

            # if restoring, make atomic cpu simulate only a few instructions
            if options.checkpoint_restore != None:
                testsys.cpu[i].max_insts_any_thread = 1
            # Fast forward to specified location if we are not restoring
            elif options.fast_forward:
                testsys.cpu[i].max_insts_any_thread = int(options.fast_forward)
            # Fast forward to a simpoint (warning: time consuming)
            elif options.simpoint:
                if testsys.cpu[i].workload[0].simpoint == 0:
                    fatal('simpoint not found')
                testsys.cpu[i].max_insts_any_thread = \
                    testsys.cpu[i].workload[0].simpoint
            # No distance specified, just switch
            else:
                testsys.cpu[i].max_insts_any_thread = 1

            # warmup period
            if options.warmup_insts:
                switch_cpus[i].max_insts_any_thread =  options.warmup_insts

            # simulation period
            if options.max_inst:
                switch_cpus_1[i].max_insts_any_thread = options.max_inst

            if not options.caches:
                # O3 CPU must have a cache to work.
                print "O3 CPU must be used with caches"
                sys.exit(1)

            testsys.switch_cpus = switch_cpus
            testsys.switch_cpus_1 = switch_cpus_1
            switch_cpu_list = [(testsys.cpu[i], switch_cpus[i]) for i in xrange(np)]
            switch_cpu_list1 = [(switch_cpus[i], switch_cpus_1[i]) for i in xrange(np)]

    # set the checkpoint in the cpu before m5.instantiate is called
    if options.take_checkpoints != None and \
           (options.simpoint or options.at_instruction):
        offset = int(options.take_checkpoints)
        # Set an instruction break point
        if options.simpoint:
            for i in xrange(np):
                if testsys.cpu[i].workload[0].simpoint == 0:
                    fatal('no simpoint for testsys.cpu[%d].workload[0]', i)
                checkpoint_inst = int(testsys.cpu[i].workload[0].simpoint) + offset
                testsys.cpu[i].max_insts_any_thread = checkpoint_inst
                # used for output below
                options.take_checkpoints = checkpoint_inst
        else:
            options.take_checkpoints = offset
            # Set all test cpus with the right number of instructions
            # for the upcoming simulation
            for i in xrange(np):
                testsys.cpu[i].max_insts_any_thread = offset

    m5.instantiate(root)

    if options.checkpoint_restore != None:
        from os.path import isdir, exists
        from os import listdir
        import re

        if not isdir(cptdir):
            fatal("checkpoint dir %s does not exist!", cptdir)

        if options.at_instruction:
            checkpoint_dir = joinpath(cptdir, "cpt.%s.%s" % \
                    (options.bench, options.checkpoint_restore))
            if not exists(checkpoint_dir):
                fatal("Unable to find checkpoint directory %s", checkpoint_dir)

            print "Restoring checkpoint ..."
            m5.restoreCheckpoint(root, checkpoint_dir)
            print "Done."
        elif options.simpoint:
            # assume workload 0 has the simpoint
            if testsys.cpu[0].workload[0].simpoint == 0:
                fatal('Unable to find simpoint')

            options.checkpoint_restore += \
                int(testsys.cpu[0].workload[0].simpoint)

            checkpoint_dir = joinpath(cptdir, "cpt.%s.%d" % \
                    (options.bench, options.checkpoint_restore))
            if not exists(checkpoint_dir):
                fatal("Unable to find checkpoint directory %s.%s",
                      options.bench, options.checkpoint_restore)

            print "Restoring checkpoint ..."
            m5.restoreCheckpoint(root,checkpoint_dir)
            print "Done."
        else:
            dirs = listdir(cptdir)
            expr = re.compile('cpt\.([0-9]*)')
            cpts = []
            for dir in dirs:
                match = expr.match(dir)
                if match:
                    cpts.append(match.group(1))

            cpts.sort(lambda a,b: cmp(long(a), long(b)))

            cpt_num = options.checkpoint_restore

            if cpt_num > len(cpts):
                fatal('Checkpoint %d not found', cpt_num)

            ## Adjust max tick based on our starting tick
            maxtick = maxtick - int(cpts[cpt_num - 1])

            ## Restore the checkpoint
            m5.restoreCheckpoint(root,
                    joinpath(cptdir, "cpt.%s" % cpts[cpt_num - 1]))

    if options.standard_switch or cpu_class:
        if options.standard_switch:
            print "Switch at instruction count:%s" % \
                    str(testsys.cpu[0].max_insts_any_thread)
            exit_event = m5.simulate()
        elif cpu_class and options.fast_forward:
            print "Switch at instruction count:%s" % \
                    str(testsys.cpu[0].max_insts_any_thread)
            exit_event = m5.simulate()
        else:
            print "Switch at curTick count:%s" % str(10000)
            exit_event = m5.simulate(10000)
        print "Switched CPUS @ cycle = %s" % (m5.curTick())

        # when you change to Timing (or Atomic), you halt the system
        # given as argument.  When you are finished with the system
        # changes (including switchCpus), you must resume the system
        # manually.  You DON'T need to resume after just switching
        # CPUs if you haven't changed anything on the system level.

        m5.changeToTiming(testsys)
        m5.switchCpus(switch_cpu_list)
        m5.resume(testsys)

        if options.standard_switch:
            print "Switch at instruction count:%d" % \
                    (testsys.switch_cpus[0].max_insts_any_thread)

            #warmup instruction count may have already been set
            if options.warmup_insts:
                exit_event = m5.simulate()
            else:
                exit_event = m5.simulate(options.warmup)
            print "Switching CPUS @ cycle = %s" % (m5.curTick())
            print "Simulation ends instruction count:%d" % \
                    (testsys.switch_cpus_1[0].max_insts_any_thread)
            m5.drain(testsys)
            m5.switchCpus(switch_cpu_list1)
            m5.resume(testsys)

    num_checkpoints = 0
    exit_cause = ''

    # If we're taking and restoring checkpoints, use checkpoint_dir
    # option only for finding the checkpoints to restore from.  This
    # lets us test checkpointing by restoring from one set of
    # checkpoints, generating a second set, and then comparing them.
    if options.take_checkpoints and options.checkpoint_restore:
        if m5.options.outdir:
            cptdir = m5.options.outdir
        else:
            cptdir = getcwd()

    # Checkpoints being taken via the command line at <when> and at
    # subsequent periods of <period>.  Checkpoint instructions
    # received from the benchmark running are ignored and skipped in
    # favor of command line checkpoint instructions.
    if options.take_checkpoints != None :
        if options.at_instruction or options.simpoint:
            checkpoint_inst = int(options.take_checkpoints)

            # maintain correct offset if we restored from some instruction
            if options.checkpoint_restore != None:
                checkpoint_inst += options.checkpoint_restore

            print "Creating checkpoint at inst:%d" % (checkpoint_inst)
            exit_event = m5.simulate()
            print "exit cause = %s" % (exit_event.getCause())

            # skip checkpoint instructions should they exist
            while exit_event.getCause() == "checkpoint":
                exit_event = m5.simulate()

            if exit_event.getCause() == \
                   "a thread reached the max instruction count":
                m5.checkpoint(root, joinpath(cptdir, "cpt.%s.%d" % \
                        (options.bench, checkpoint_inst)))
                print "Checkpoint written."
                num_checkpoints += 1

            if exit_event.getCause() == "user interrupt received":
                exit_cause = exit_event.getCause();
        else:
            when, period = options.take_checkpoints.split(",", 1)
            when = int(when)
            period = int(period)

            exit_event = m5.simulate(when)
            while exit_event.getCause() == "checkpoint":
                exit_event = m5.simulate(when - m5.curTick())

            if exit_event.getCause() == "simulate() limit reached":
                m5.checkpoint(root, joinpath(cptdir, "cpt.%d"))
                num_checkpoints += 1

            sim_ticks = when
            exit_cause = "maximum %d checkpoints dropped" % max_checkpoints
            while num_checkpoints < max_checkpoints and \
                    exit_event.getCause() == "simulate() limit reached":
                if (sim_ticks + period) > maxtick:
                    exit_event = m5.simulate(maxtick - sim_ticks)
                    exit_cause = exit_event.getCause()
                    break
                else:
                    exit_event = m5.simulate(period)
                    sim_ticks += period
                    while exit_event.getCause() == "checkpoint":
                        exit_event = m5.simulate(sim_ticks - m5.curTick())
                    if exit_event.getCause() == "simulate() limit reached":
                        m5.checkpoint(root, joinpath(cptdir, "cpt.%d"))
                        num_checkpoints += 1

            if exit_event.getCause() != "simulate() limit reached":
                exit_cause = exit_event.getCause();

    else: # no checkpoints being taken via this script
        if options.fast_forward:
            m5.stats.reset()
        print "**** REAL SIMULATION ****"
        exit_event = m5.simulate(maxtick)

        while exit_event.getCause() == "checkpoint":
            m5.checkpoint(root, joinpath(cptdir, "cpt.%d"))
            num_checkpoints += 1
            if num_checkpoints == max_checkpoints:
                exit_cause = "maximum %d checkpoints dropped" % max_checkpoints
                break

            exit_event = m5.simulate(maxtick - m5.curTick())
            exit_cause = exit_event.getCause()

    if exit_cause == '':
        exit_cause = exit_event.getCause()
    print 'Exiting @ cycle %i because %s' % (m5.curTick(), exit_cause)

    if options.checkpoint_at_end:
        m5.checkpoint(root, joinpath(cptdir, "cpt.%d"))

