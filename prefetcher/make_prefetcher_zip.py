#!/usr/bin/env python

import re
from zipfile import ZipFile

def storage_usage(num_deltas, table_size, delta_bits):
    entry_size = 3 * 28 + delta_bits * num_deltas
    return table_size * entry_size

# (NUM_DELTAS, TABLE_SIZE, DELTA_BITS, MAX_DEGREE) 

params = [
    # Try nearby NUM_DELTAS
    (31,  98, 12, "NUM_DELTAS"),
    (32,  98, 12, "NUM_DELTAS"),
    (33,  98, 12, "NUM_DELTAS"),
    (34,  98, 12, "NUM_DELTAS"),
    (36,  98, 12, "NUM_DELTAS"),
    (37,  98, 12, "NUM_DELTAS"),
    (38,  98, 12, "NUM_DELTAS"),
    (39,  98, 12, "NUM_DELTAS"),

    # Try nearby TABLE_SIZES
    (35,  95, 12, "NUM_DELTAS"),
    (35,  96, 12, "NUM_DELTAS"),
    (35,  97, 12, "NUM_DELTAS"),
    (35,  99, 12, "NUM_DELTAS"),
    (35, 100, 12, "NUM_DELTAS"),
    (35, 101, 12, "NUM_DELTAS"),
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
