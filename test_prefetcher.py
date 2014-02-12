#!/usr/bin/env python
"""
Run the simulator and record statistics.
"""

import sys, os, os.path, glob

from lib.run_util import *
import lib.stats as stats

# Uncomment this to print commmmmands instead of executing them.
#dry_run()


# Set paths
m5_path('m5/build/ALPHA_SE/m5.opt')
se_path('m5/configs/example/se.py')

# Check that M5 is compiled
if not os.path.exists('m5/build/ALPHA_SE/m5.opt'):
    print >>sys.stderr, "Could not find the M5 binary, run compile.sh to compile with your prefetcher."
    sys.exit(1)
print "Remember to recompile after making changes."

# Set output directory
global_prefix('output/')

# Configure
global_args(
    '--checkpoint-dir=lib/cp',
    '--checkpoint-restore=%d' % 1e9, '--at-instruction',
    '--caches', '--l2cache',
    '--standard-switch', '--warmup-insts=%d' % 1e7,
    '--max-inst=%d' % 1e7,

    '--l2size=1MB',
    '--membus-width=8', '--membus-clock=400MHz', '--mem-latency=30ns',
)

# Prefetchers to run
prefetchers = Config('user', ['--prefetcher=on_access=true:policy=proxy'])

# Tests to run
tests = spec_configs
#tests = spec_configs[:2]

configs = cross(tests, prefetchers)

# Run tests
os.chdir(os.path.dirname(__file__))
os.environ['M5_CPU2000'] = 'lib/cpu2000'
run_configs(configs)

# Read statistics
stats.BASELINE_PF = 'none'
pf_stats = stats.read_stats(*glob.glob('lib/stats/*_1e7'))

pf_stats.update(stats.build_stats('output'))

# Write statistics
stats_file = open('stats.txt', 'w')
def save_stats(pf, test, echo):
    table = stats.format_stats(pf_stats, pf, test)
    stats_file.write(table)
    if echo:
        print table

# Prefetcher comparison for each test
for test in sorted(pf_stats['user']):
    save_stats('all', test, False)
# User prefetcher results.
save_stats('user', 'all', True)
# Summary
save_stats('all', 'all', True)

stats_file.close()
