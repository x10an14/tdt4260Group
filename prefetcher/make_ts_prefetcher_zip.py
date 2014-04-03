#!/usr/bin/env python

import re
from zipfile import ZipFile

# (DISTANCE, DEGREE) 

#params = [
#    (42, 1337),
#]

params = [(distance, degree) for distance in range(14, 21, 2) for degree in range(1, 5)]

source = open("ts_54.cc").read() 

with ZipFile("ts_prefetchers.zip", "w") as z:
    for (distance, degree) in params:
        name = "ts_%s_%s.cc" % (distance, degree)

        print "Creating %s" % name

        s = source
        s = re.sub("#define DISTANCE .*", "#define DISTANCE %s" % distance, s)
        s = re.sub("#define DEGREE .*",   "#define DEGREE %s"   % degree,   s)

        z.writestr(name, s)
