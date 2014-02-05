/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Nathan Binkert
 */

#include <iomanip>
#include <fstream>
#include <list>
#include <map>
#include <string>

#include "base/callback.hh"
#include "base/cprintf.hh"
#include "base/debug.hh"
#include "base/hostinfo.hh"
#include "base/misc.hh"
#include "base/statistics.hh"
#include "base/str.hh"
#include "base/time.hh"
#include "base/trace.hh"

using namespace std;

namespace Stats {

typedef map<const void *, Info *> MapType;

// We wrap these in a function to make sure they're built in time.
list<Info *> &
statsList()
{
    static list<Info *> the_list;
    return the_list;
}

MapType &
statsMap()
{
    static MapType the_map;
    return the_map;
}

void
InfoAccess::setInfo(Info *info)
{
    if (statsMap().find(this) != statsMap().end())
        panic("shouldn't register stat twice!");

    statsList().push_back(info);

#ifndef NDEBUG
    pair<MapType::iterator, bool> result =
#endif
        statsMap().insert(make_pair(this, info));
    assert(result.second && "this should never fail");
    assert(statsMap().find(this) != statsMap().end());
}

void
InfoAccess::setParams(const StorageParams *params)
{
    info()->storageParams = params;
}

void
InfoAccess::setInit()
{
    info()->flags.set(init);
}

Info *
InfoAccess::info()
{
    MapType::const_iterator i = statsMap().find(this);
    assert(i != statsMap().end());
    return (*i).second;
}

const Info *
InfoAccess::info() const
{
    MapType::const_iterator i = statsMap().find(this);
    assert(i != statsMap().end());
    return (*i).second;
}

StorageParams::~StorageParams()
{
}

typedef map<std::string, Info *> NameMapType;
NameMapType &
nameMap()
{
    static NameMapType the_map;
    return the_map;
}

int Info::id_count = 0;

int debug_break_id = -1;

Info::Info()
    : flags(none), precision(-1), prereq(0), storageParams(NULL)
{
    id = id_count++;
    if (debug_break_id >= 0 and debug_break_id == id)
        debug_break();
}

Info::~Info()
{
}

void
Info::setName(const string &name)
{
    pair<NameMapType::iterator, bool> p =
        nameMap().insert(make_pair(name, this));

    Info *other = p.first->second;
    bool result = p.second;
    
    if (!result) {
        // using other->name instead of just name to avoid a compiler
        // warning.  They should be the same.
        panic("same statistic name used twice! name=%s\n", other->name);
    }

    this->name = name;
}

bool
Info::less(Info *stat1, Info *stat2)
{
    const string &name1 = stat1->name;
    const string &name2 = stat2->name;

    vector<string> v1;
    vector<string> v2;

    tokenize(v1, name1, '.');
    tokenize(v2, name2, '.');

    size_type last = min(v1.size(), v2.size()) - 1;
    for (off_type i = 0; i < last; ++i)
        if (v1[i] != v2[i])
            return v1[i] < v2[i];

    // Special compare for last element.
    if (v1[last] == v2[last])
        return v1.size() < v2.size();
    else
        return v1[last] < v2[last];

    return false;
}

bool
Info::baseCheck() const
{
    if (!(flags & Stats::init)) {
#ifdef DEBUG
        cprintf("this is stat number %d\n", id);
#endif
        panic("Not all stats have been initialized");
        return false;
    }

    if ((flags & display) && name.empty()) {
        panic("all printable stats must be named");
        return false;
    }

    return true;
}

void
Info::enable()
{
}

void
VectorInfo::enable()
{
    size_type s = size();
    if (subnames.size() < s)
        subnames.resize(s);
    if (subdescs.size() < s)
        subdescs.resize(s);
}

void
VectorDistInfo::enable()
{
    size_type s = size();
    if (subnames.size() < s)
        subnames.resize(s);
    if (subdescs.size() < s)
        subdescs.resize(s);
}

void
Vector2dInfo::enable()
{
    if (subnames.size() < x)
        subnames.resize(x);
    if (subdescs.size() < x)
        subdescs.resize(x);
    if (y_subnames.size() < y)
        y_subnames.resize(y);
}

Formula::Formula()
{
}

Formula::Formula(Temp r)
{
    root = r;
    setInit();
    assert(size());
}

const Formula &
Formula::operator=(Temp r)
{
    assert(!root && "Can't change formulas");
    root = r;
    setInit();
    assert(size());
    return *this;
}

const Formula &
Formula::operator+=(Temp r)
{
    if (root)
        root = NodePtr(new BinaryNode<std::plus<Result> >(root, r));
    else {
        root = r;
        setInit();
    }

    assert(size());
    return *this;
}

void
Formula::result(VResult &vec) const
{
    if (root)
        vec = root->result();
}

Result
Formula::total() const
{
    return root ? root->total() : 0.0;
}

size_type
Formula::size() const
{
    if (!root)
        return 0;
    else
        return root->size();
}

void
Formula::reset()
{
}

bool
Formula::zero() const
{
    VResult vec;
    result(vec);
    for (VResult::size_type i = 0; i < vec.size(); ++i)
        if (vec[i] != 0.0)
            return false;
    return true;
}

string
Formula::str() const
{
    return root ? root->str() : "";
}

void
enable()
{
    typedef list<Info *>::iterator iter_t;

    iter_t i, end = statsList().end();
    for (i = statsList().begin(); i != end; ++i) {
        Info *info = *i;
        assert(info);
        if (!info->check() || !info->baseCheck())
            panic("stat check failed for '%s' %d\n", info->name, info->id);
    }

    off_t j = 0;
    for (i = statsList().begin(); i != end; ++i) {
        Info *info = *i;
        if (!(info->flags & display))
            info->name = "__Stat" + to_string(j++);
    }

    statsList().sort(Info::less);

    for (i = statsList().begin(); i != end; ++i) {
        Info *info = *i;
        info->enable();
    }
}

void
prepare()
{
    list<Info *>::iterator i = statsList().begin();
    list<Info *>::iterator end = statsList().end();
    while (i != end) {
        Info *info = *i;
        info->prepare();
        ++i;
    }
}

CallbackQueue resetQueue;

void
reset()
{
    list<Info *>::iterator i = statsList().begin();
    list<Info *>::iterator end = statsList().end();
    while (i != end) {
        Info *info = *i;
        info->reset();
        ++i;
    }

    resetQueue.process();
}

void
registerResetCallback(Callback *cb)
{
    resetQueue.add(cb);
}

/* namespace Stats */ }
