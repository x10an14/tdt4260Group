#! /usr/bin/env python
# Copyright (c) 2006 The Regents of The University of Michigan
# Copyright (c) 2007 The Hewlett-Packard Development Company
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
# Authors: Nathan Binkert

import re
import os
import sys

lead = re.compile(r'^([ \t]+)')
trail = re.compile(r'([ \t]+)$')
any_control = re.compile(r'\b(if|while|for)[ \t]*[(]')
good_control = re.compile(r'\b(if|while|for) [(]')

lang_types = { 'c'   : "C",
               'h'   : "C",
               'cc'  : "C++",
               'hh'  : "C++",
               'cxx' : "C++",
               'hxx' : "C++",
               'cpp' : "C++",
               'hpp' : "C++",
               'C'   : "C++",
               'H'   : "C++",
               'i'   : "swig",
               'py'  : "python",
               's'   : "asm",
               'S'   : "asm",
               'isa' : "isa" }
def file_type(filename):
    extension = filename.split('.')
    extension = len(extension) > 1 and extension[-1]
    return lang_types.get(extension, None)

whitespace_types = ('C', 'C++', 'swig', 'python', 'asm', 'isa')
def whitespace_file(filename):
    if file_type(filename) in whitespace_types:
        return True

    if filename.startswith("SCons"):
        return True

    return False

format_types = ( 'C', 'C++' )
def format_file(filename):
    if file_type(filename) in format_types:
        return True

    return True 

def checkwhite_line(line):
    match = lead.search(line)
    if match and match.group(1).find('\t') != -1:
        return False

    match = trail.search(line)
    if match:
        return False

    return True

def checkwhite(filename):
    if not whitespace_file(filename):
        return

    try:
        f = file(filename, 'r+')
    except OSError, msg:
        print 'could not open file %s: %s' % (filename, msg)
        return

    for num,line in enumerate(f):
        if not checkwhite_line(line):
            yield line,num + 1

def fixwhite_line(line, tabsize):
    if lead.search(line):
        newline = ''
        for i,c in enumerate(line):
            if c == ' ':
                newline += ' '
            elif c == '\t':
                newline += ' ' * (tabsize - len(newline) % tabsize)
            else:
                newline += line[i:]
                break

        line = newline

    return line.rstrip() + '\n'

def fixwhite(filename, tabsize, fixonly=None):
    if not whitespace_file(filename):
        return

    try:
        f = file(filename, 'r+')
    except OSError, msg:
        print 'could not open file %s: %s' % (filename, msg)
        return

    lines = list(f)

    f.seek(0)
    f.truncate()

    for i,line in enumerate(lines):
        if fixonly is None or i in fixonly:
            line = fixwhite_line(line, tabsize)

        print >>f, line,

def linelen(line):
    tabs = line.count('\t')
    if not tabs:
        return len(line)

    count = 0
    for c in line:
        if c == '\t':
            count += tabsize - count % tabsize
        else:
            count += 1

    return count

class ValidationStats(object):
    def __init__(self):
        self.toolong = 0
        self.toolong80 = 0
        self.leadtabs = 0
        self.trailwhite = 0
        self.badcontrol = 0
        self.cret = 0

    def dump(self):
        print '''\
%d violations of lines over 79 chars. %d of which are 80 chars exactly.
%d cases of whitespace at the end of a line.
%d cases of tabs to indent.
%d bad parens after if/while/for.
%d carriage returns found.
''' % (self.toolong, self.toolong80, self.trailwhite, self.leadtabs,
       self.badcontrol, self.cret)

    def __nonzero__(self):
        return self.toolong or self.toolong80 or self.leadtabs or \
               self.trailwhite or self.badcontrol or self.cret

def validate(filename, stats, verbose, exit_code):
    if not format_file(filename):
        return

    def msg(lineno, line, message):
        print '%s:%d>' % (filename, lineno + 1), message
        if verbose > 2:
            print line

    def bad():
        if exit_code is not None:
            sys.exit(exit_code)

    cpp = filename.endswith('.cc') or filename.endswith('.hh')
    py = filename.endswith('.py')

    if py + cpp != 1:
        raise AttributeError, \
              "I don't know how to deal with the file %s" % filename

    try:
        f = file(filename, 'r')
    except OSError:
        if verbose > 0:
            print 'could not open file %s' % filename
        bad()
        return

    for i,line in enumerate(f):
        line = line.rstrip('\n')

        # no carriage returns
        if line.find('\r') != -1:
            self.cret += 1
            if verbose > 1:
                msg(i, line, 'carriage return found')
            bad()

        # lines max out at 79 chars
        llen = linelen(line)
        if llen > 79:
            stats.toolong += 1
            if llen == 80:
                stats.toolong80 += 1
            if verbose > 1:
                msg(i, line, 'line too long (%d chars)' % llen)
            bad()

        # no tabs used to indent
        match = lead.search(line)
        if match and match.group(1).find('\t') != -1:
            stats.leadtabs += 1
            if verbose > 1:
                msg(i, line, 'using tabs to indent')
            bad()

        # no trailing whitespace
        if trail.search(line):
            stats.trailwhite +=1
            if verbose > 1:
                msg(i, line, 'trailing whitespace')
            bad()

        # for c++, exactly one space betwen if/while/for and (
        if cpp:
            match = any_control.search(line)
            if match and not good_control.search(line):
                stats.badcontrol += 1
                if verbose > 1:
                    msg(i, line, 'improper spacing after %s' % match.group(1))
                bad()

def modified_lines(old_data, new_data, max_lines):
    from itertools import count
    from mercurial import bdiff, mdiff

    modified = set()
    counter = count()
    for pbeg, pend, fbeg, fend in bdiff.blocks(old_data, new_data):
        for i in counter:
            if i < fbeg:
                modified.add(i)
            elif i + 1 >= fend:
                break
            elif i > max_lines:
                break
    return modified

def do_check_whitespace(ui, repo, *files, **args):
    """check files for proper m5 style guidelines"""
    from mercurial import mdiff, util

    if files:
        files = frozenset(files)

    def skip(name):
        return files and name in files

    def prompt(name, fixonly=None):
        if args.get('auto', False):
            result = 'f'
        else:
            while True:
                result = ui.prompt("(a)bort, (i)gnore, or (f)ix?", default='a')
                if result in 'aif':
                    break

        if result == 'a':
            return True
        elif result == 'f':
            fixwhite(repo.wjoin(name), args['tabsize'], fixonly)

        return False

    modified, added, removed, deleted, unknown, ignore, clean = repo.status()

    for fname in added:
        if skip(fname):
            continue

        ok = True
        for line,num in checkwhite(repo.wjoin(fname)):
            ui.write("invalid whitespace in %s:%d\n" % (fname, num))
            if ui.verbose:
                ui.write(">>%s<<\n" % line[-1])
            ok = False

        if not ok:
            if prompt(fname):
                return True

    try:
        wctx = repo.workingctx()
    except:
        from mercurial import context
        wctx = context.workingctx(repo)

    for fname in modified:
        if skip(fname):
            continue

        if not whitespace_file(fname):
            continue

        fctx = wctx.filectx(fname)
        pctx = fctx.parents()

        file_data = fctx.data()
        lines = mdiff.splitnewlines(file_data)
        if len(pctx) in (1, 2):
            mod_lines = modified_lines(pctx[0].data(), file_data, len(lines))
            if len(pctx) == 2:
                m2 = modified_lines(pctx[1].data(), file_data, len(lines))
                # only the lines that are new in both
                mod_lines = mod_lines & m2
        else:
            mod_lines = xrange(0, len(lines))

        fixonly = set()
        for i,line in enumerate(lines):
            if i not in mod_lines:
                continue

            if checkwhite_line(line):
                continue

            ui.write("invalid whitespace: %s:%d\n" % (fname, i+1))
            if ui.verbose:
                ui.write(">>%s<<\n" % line[:-1])
            fixonly.add(i)

        if fixonly:
            if prompt(fname, fixonly):
                return True

def check_whitespace(ui, repo, hooktype, node, parent1, parent2, **kwargs):
    if hooktype != 'pretxncommit':
        raise AttributeError, \
              "This hook is only meant for pretxncommit, not %s" % hooktype

    args = { 'tabsize' : 8 }
    return do_check_whitespace(ui, repo, **args)

def check_format(ui, repo, hooktype, node, parent1, parent2, **kwargs):
    if hooktype != 'pretxncommit':
        raise AttributeError, \
              "This hook is only meant for pretxncommit, not %s" % hooktype

    modified, added, removed, deleted, unknown, ignore, clean = repo.status()

    verbose = 0
    stats = ValidationStats()
    for f in modified + added:
        validate(f, stats, verbose, None)

    if stats:
        stats.dump()
        result = ui.prompt("invalid formatting\n(i)gnore or (a)bort?",
                           "^[ia]$", "a")
        if result.startswith('i'):
            pass
        elif result.startswith('a'):
            return True
        else:
            raise util.Abort(_("Invalid response: '%s'") % result)

    return False

try:
    from mercurial.i18n import _
except ImportError:
    def _(arg):
        return arg

cmdtable = {
    '^m5style' :
    ( do_check_whitespace,
      [ ('a', 'auto', False, _("automatically fix whitespace")),
        ('t', 'tabsize', 8, _("Number of spaces TAB indents")) ],
      _('hg m5check [-t <tabsize>] [FILE]...')),
}
if __name__ == '__main__':
    import getopt

    progname = sys.argv[0]
    if len(sys.argv) < 2:
        sys.exit('usage: %s <command> [<command args>]' % progname)

    fixwhite_usage = '%s fixwhite [-t <tabsize> ] <path> [...] \n' % progname
    chkformat_usage = '%s chkformat <path> [...] \n' % progname
    chkwhite_usage = '%s chkwhite <path> [...] \n' % progname

    command = sys.argv[1]
    if command == 'fixwhite':
        flags = 't:'
        usage = fixwhite_usage
    elif command == 'chkwhite':
        flags = 'nv'
        usage = chkwhite_usage
    elif command == 'chkformat':
        flags = 'nv'
        usage = chkformat_usage
    else:
        sys.exit(fixwhite_usage + chkwhite_usage + chkformat_usage)

    opts, args = getopt.getopt(sys.argv[2:], flags)

    code = 1
    verbose = 1
    tabsize = 8
    for opt,arg in opts:
        if opt == '-n':
            code = None
        if opt == '-t':
            tabsize = int(arg)
        if opt == '-v':
            verbose += 1

    if command == 'fixwhite':
        for filename in args:
            fixwhite(filename, tabsize)
    elif command == 'chkwhite':
        for filename in args:
            for line,num in checkwhite(filename):
                print 'invalid whitespace: %s:%d' % (filename, num)
                if verbose:
                    print '>>%s<<' % line[:-1]
    elif command == 'chkformat':
        stats = ValidationStats()
        for filename in args:
            validate(filename, stats=stats, verbose=verbose, exit_code=code)

        if verbose > 0:
            stats.dump()
    else:
        sys.exit("command '%s' not found" % command)
