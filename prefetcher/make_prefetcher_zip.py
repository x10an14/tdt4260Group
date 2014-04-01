#!/usr/bin/env python

import re
from zipfile import ZipFile

def storage_usage(num_deltas, table_size, delta_bits):
    entry_size = 3 * 28 + delta_bits * num_deltas
    return table_size * entry_size

# (NUM_DELTAS, TABLE_SIZE, DELTA_BITS, MAX_DEGREE) 

params = [
    # Additional NUM_DELTAS data
    (16, 98, 12, "NUM_DELTAS"),
    (19, 98, 12, "NUM_DELTAS"),
    (21, 98, 12, "NUM_DELTAS"),
    (22, 98, 12, "NUM_DELTAS"),
    (30, 98, 12, "NUM_DELTAS"),
    (35, 98, 12, "NUM_DELTAS"),
    (40, 98, 12, "NUM_DELTAS"),
    (45, 98, 12, "NUM_DELTAS"),

    # DELTA_BITS 
    (19, 98,  8, "NUM_DELTAS"),
    (19, 98,  9, "NUM_DELTAS"),
    (19, 98, 10, "NUM_DELTAS"),
    (19, 98, 15, "NUM_DELTAS"),

    # TABLE_SIZE
    (19,  90, 12, "NUM_DELTAS"),
    (19,  95, 12, "NUM_DELTAS"),
    (19,  96, 12, "NUM_DELTAS"),
    (19, 100, 12, "NUM_DELTAS"),
    (19, 101, 12, "NUM_DELTAS"),
    (19, 120, 12, "NUM_DELTAS"),
    (19, 140, 12, "NUM_DELTAS"),
    (19, 160, 12, "NUM_DELTAS"),
    (19, 180, 12, "NUM_DELTAS"),

    # MAX_DEGREE
    (19,  98, 12, 5),
    (19,  98, 12, 15),
    (19,  98, 12, 18),
    (19,  98, 12, 20),
    (19,  98, 12, 25),

    # BEST
    (15,  98, 17, "NUM_DELTAS"),
    (20,  98, 17, "NUM_DELTAS"),
    (25,  98, 17, "NUM_DELTAS"),
    (30,  98, 17, "NUM_DELTAS"),
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
