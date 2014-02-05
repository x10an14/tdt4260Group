#!/usr/bin/env python
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
# Authors: Ali Saidi
#          Steve Reinhardt
#          Nathan Binkert

import os
import re
import shutil
import sys
import time

from glob import glob
from os import system
from os.path import basename, dirname, exists, isdir, isfile, join as joinpath

def mkdir(*args):
    path = joinpath(*args)
    os.mkdir(path)

def touch(*args, **kwargs):
    when = kwargs.get('when', None)
    path = joinpath(*args)
    os.utime(path, when)

def rmtree(*args):
    path = joinpath(*args)
    for match in glob(path):
        if isdir(match):
            shutil.rmtree(match)
        else:
            os.unlink(match)

def remove(*args):
    path = joinpath(*args)
    for match in glob(path):
        if not isdir(match):
            os.unlink(match)

def movedir(srcdir, destdir, dir):
    src = joinpath(srcdir, dir)
    dest = joinpath(destdir, dir)

    if not isdir(src):
        raise AttributeError

    os.makedirs(dirname(dest))
    shutil.move(src, dest)

if not isdir('.hg'):
    sys.exit('Not in the top level of an m5 tree!')

usage = '%s <destdir> <release name>' % sys.argv[0]

if len(sys.argv) != 3:
    sys.exit(usage)

destdir = sys.argv[1]
releasename = sys.argv[2]
release_dest = joinpath(destdir, 'release')
#encumbered_dest = joinpath(destdir, 'encumbered')
release_dir = joinpath(release_dest, releasename)
#encumbered_dir = joinpath(encumbered_dest, releasename)

if exists(destdir):
    if not isdir(destdir):
        raise AttributeError, '%s exists, but is not a directory' % destdir
else:
    mkdir(destdir)

if exists(release_dest):
    if not isdir(release_dest):
        raise AttributeError, \
              '%s exists, but is not a directory' % release_dest
    rmtree(release_dest)

#if exists(encumbered_dest):
#    if not isdir(encumbered_dest):
#       raise AttributeError, \
#             '%s exists, but is not a directory' % encumbered_dest
#   rmtree(encumbered_dest)

mkdir(release_dest)
#mkdir(encumbered_dest)
mkdir(release_dir)
#mkdir(encumbered_dir)

system('hg update')
system('rsync -av --exclude ".hg*" --exclude build . %s' % release_dir)
# move the time forward on some files by a couple of minutes so we can
# avoid building things unnecessarily
when = int(time.time()) + 120

# make sure scons doesn't try to run flex unnecessarily
#touch(release_dir, 'src/encumbered/eio/exolex.cc', when=(when, when))

# get rid of non-shipping code
#rmtree(release_dir, 'src/encumbered/dev')
rmtree(release_dir, 'src/cpu/ozone')
#rmtree(release_dir, 'src/mem/cache/tags/split*.cc')
#rmtree(release_dir, 'src/mem/cache/tags/split*.hh')
#rmtree(release_dir, 'src/mem/cache/prefetch/ghb_*.cc')
#rmtree(release_dir, 'src/mem/cache/prefetch/ghb_*.hh')
#rmtree(release_dir, 'src/mem/cache/prefetch/stride_*.cc')
#rmtree(release_dir, 'src/mem/cache/prefetch/stride_*.hh')
rmtree(release_dir, 'configs/fullsys')
rmtree(release_dir, 'configs/test')
rmtree(release_dir, 'tests/long/*/ref')
rmtree(release_dir, 'tests/old')
rmtree(release_dir, 'tests/quick/00.hello/ref/x86')
rmtree(release_dir, 'tests/quick/02.insttest')
rmtree(release_dir, 'tests/test-progs/hello/bin/x86')

remove(release_dir, 'src/cpu/nativetrace.hh')
remove(release_dir, 'src/cpu/nativetrace.cc')

# get rid of some of private scripts
remove(release_dir, 'util/chgcopyright')
remove(release_dir, 'util/make_release.py')

def remove_sources(regex, subdir):
    script = joinpath(release_dir, subdir, 'SConscript')
    if isinstance(regex, str):
        regex = re.compile(regex)
    inscript = file(script, 'r').readlines()
    outscript = file(script, 'w')
    for line in inscript:
        if regex.match(line):
            continue

        outscript.write(line)
    outscript.close()

def remove_lines(s_regex, e_regex, f):
    f = joinpath(release_dir, f)
    if isinstance(s_regex, str):
        s_regex = re.compile(s_regex)
    if isinstance(e_regex, str):
        e_regex = re.compile(e_regex)
    inscript = file(f, 'r').readlines()
    outscript = file(f, 'w')
    skipping = False
    for line in inscript:
        if (not skipping and s_regex.match(line)) or \
                (e_regex and skipping and not e_regex.match(line)):
            if e_regex:
                skipping = True
            continue
        skipping = False
        outscript.write(line)
    outscript.close()

def replace_line(s_regex, f, rl):
    f = joinpath(release_dir, f)
    if isinstance(s_regex, str):
        s_regex = re.compile(s_regex)
    inscript = file(f, 'r').readlines()
    outscript = file(f, 'w')
    for line in inscript:
        if s_regex.match(line):
            outscript.write(rl)
            continue
        outscript.write(line)
    outscript.close()


# fix up the SConscript to deal with files we've removed
#remove_sources(r'.*split.*\.cc', 'src/mem/cache/tags')
#remove_sources(r'.*(ghb|stride)_prefetcher\.cc', 'src/mem/cache/prefetch')
remove_sources(r'.*nativetrace.*', 'src/cpu')

benches = [ 'bzip2', 'eon', 'gzip', 'mcf', 'parser', 'perlbmk',
            'twolf', 'vortex' ]
for bench in benches:
    rmtree(release_dir, 'tests', 'test-progs', bench)

#movedir(release_dir, encumbered_dir, 'src/encumbered')
rmtree(release_dir, 'tests/test-progs/anagram')
rmtree(release_dir, 'tests/quick/20.eio-short')

f = open('src/cpu/SConsopts', 'w+')
f.writelines(("Import('*')\n", "all_cpu_list.append('DummyCPUMakeSconsHappy')\n"))
f.close()


def taritup(directory, destdir, filename):
    basedir = dirname(directory)
    tarball = joinpath(destdir, filename)
    tardir = basename(directory)

    system('cd %s; tar cfj %s %s' % (basedir, tarball, tardir))

taritup(release_dir, destdir, '%s.tar.bz2' % releasename)
#taritup(encumbered_dir, destdir, '%s-encumbered.tar.bz2' % releasename)

print "release created in %s" % destdir
print "don't forget to tag the repository!"
