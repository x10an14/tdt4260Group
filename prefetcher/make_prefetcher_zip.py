#!/usr/bin/env python

import re
from zipfile import ZipFile

def storage_usage(num_deltas, table_size, delta_bits):
    entry_size = 3 * 28 + delta_bits * num_deltas
    return table_size * entry_size

# (NUM_DELTAS, TABLE_SIZE, DELTA_BITS, MAX_DEGREE) 

params = [
    (19, 98,  12, "NUM_DELTAS"), # settings from Magnu's paper
    (19, 196, 12, "NUM_DELTAS"), # double # of entries
    (19, 196, 12, 1),            # degree = 1
    (19, 196, 12, 10),           # degree = 10
    (48, 98,  12, "NUM_DELTAS"), # higher number of deltas
    (19, 98,  28, "NUM_DELTAS"), # big deltas

    # hill climbing (1 step)
    (18, 98,  12, "NUM_DELTAS"), # NUM_DELTAS -1
    (20, 98,  12, "NUM_DELTAS"), # NUM_DELTAS +1
    (19, 97,  12, "NUM_DELTAS"), # TABLE_SIZE -1
    (19, 99,  12, "NUM_DELTAS"), # TABLE_SIZE +1
    (19, 98,  11, "NUM_DELTAS"), # DELTA_BITS -1
    (19, 98,  13, "NUM_DELTAS"), # DELTA_BITS +1

    # hill climbing (5 steps)
    (14, 98,  12, "NUM_DELTAS"), # NUM_DELTAS -5
    (24, 98,  12, "NUM_DELTAS"), # NUM_DELTAS +5
    (19, 93,  12, "NUM_DELTAS"), # TABLE_SIZE -5
    (19, 103, 12, "NUM_DELTAS"), # TABLE_SIZE +5
    (19, 98,   7, "NUM_DELTAS"), # DELTA_BITS -5
    (19, 98,  17, "NUM_DELTAS"), # DELTA_BITS +5
]

source = open("prefetcher.cc").read() 

with ZipFile("prefetchers.zip", "w") as z:
    for (num_deltas, table_size, delta_bits, max_degree) in params:
        name = "dcpt_%s_%s_%s_%s.cc" % (num_deltas, table_size, delta_bits, max_degree)

        bits = storage_usage(num_deltas, table_size, delta_bits)

        print "Creating %s (storage: %d bits = %d KiB)" % (name, bits, bits / 8192)

        if bits > 8 * 8 * 1024:
            print "***WARNING: THE ABOVE PREFETCHER USES TOO MUCH SPACE***"

        s = source
        s = re.sub("#define NUM_DELTAS .*", "#define NUM_DELTAS %s" % num_deltas, s)
        s = re.sub("#define TABLE_SIZE .*", "#define TABLE_SIZE %s" % table_size, s)
        s = re.sub("#define DELTA_BITS .*", "#define DELTA_BITS %s" % delta_bits, s)
        s = re.sub("#define MAX_DEGREE .*", "#define MAX_DEGREE %s" % max_degree, s)

        z.writestr(name, s)
