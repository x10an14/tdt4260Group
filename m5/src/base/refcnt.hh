/*
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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

#ifndef __BASE_REFCNT_HH__
#define __BASE_REFCNT_HH__

class RefCounted
{
  private:
    int count;

  private:
    // Don't allow a default copy constructor or copy operator on
    // these objects because the default operation will copy the
    // reference count as well and we certainly don't want that.
    RefCounted(const RefCounted &);
    RefCounted &operator=(const RefCounted &);

  public:
    RefCounted() : count(0) {}
    virtual ~RefCounted() {}

    void incref() { ++count; }
    void decref() { if (--count <= 0) delete this; }
};

template <class T>
class RefCountingPtr
{
  protected:
    T *data;

    void copy(T *d)
    {
        data = d;
        if (data)
            data->incref();
    }
    void del()
    {
        if (data)
            data->decref();
    }
    void set(T *d)
    {
        if (data == d)
            return;

        del();
        copy(d);
    }


  public:
    RefCountingPtr() : data(0) {}
    RefCountingPtr(T *data) { copy(data); }
    RefCountingPtr(const RefCountingPtr &r) { copy(r.data); }
    ~RefCountingPtr() { del(); }

    T *operator->() { return data; }
    T &operator*() { return *data; }
    T *get() { return data; }

    const T *operator->() const { return data; }
    const T &operator*() const { return *data; }
    const T *get() const { return data; }

    const RefCountingPtr &operator=(T *p) { set(p); return *this; }
    const RefCountingPtr &operator=(const RefCountingPtr &r)
    { return operator=(r.data); }

    bool operator!() const { return data == 0; }
    operator bool() const { return data != 0; }
};

template<class T>
bool operator==(const RefCountingPtr<T> &l, const RefCountingPtr<T> &r)
{ return l.get() == r.get(); }

template<class T>
bool operator==(const RefCountingPtr<T> &l, const T *r)
{ return l.get() == r; }

template<class T>
bool operator==(const T &l, const RefCountingPtr<T> &r)
{ return l == r.get(); }

template<class T>
bool operator!=(const RefCountingPtr<T> &l, const RefCountingPtr<T> &r)
{ return l.get() != r.get(); }

template<class T>
bool operator!=(const RefCountingPtr<T> &l, const T *r)
{ return l.get() != r; }

template<class T>
bool operator!=(const T &l, const RefCountingPtr<T> &r)
{ return l != r.get(); }

#endif // __BASE_REFCNT_HH__
