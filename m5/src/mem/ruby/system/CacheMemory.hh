/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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
 */

#ifndef __MEM_RUBY_SYSTEM_CACHEMEMORY_HH__
#define __MEM_RUBY_SYSTEM_CACHEMEMORY_HH__

#include <iostream>
#include <string>
#include <vector>

#include "base/hashmap.hh"
#include "mem/protocol/AccessPermission.hh"
#include "mem/protocol/CacheMsg.hh"
#include "mem/protocol/CacheRequestType.hh"
#include "mem/protocol/MachineType.hh"
#include "mem/ruby/common/Address.hh"
#include "mem/ruby/common/DataBlock.hh"
#include "mem/ruby/common/Global.hh"
#include "mem/ruby/profiler/CacheProfiler.hh"
#include "mem/ruby/recorder/CacheRecorder.hh"
#include "mem/ruby/slicc_interface/AbstractCacheEntry.hh"
#include "mem/ruby/slicc_interface/AbstractController.hh"
#include "mem/ruby/slicc_interface/RubySlicc_ComponentMapping.hh"
#include "mem/ruby/system/LRUPolicy.hh"
#include "mem/ruby/system/PseudoLRUPolicy.hh"
#include "mem/ruby/system/System.hh"
#include "params/RubyCache.hh"
#include "sim/sim_object.hh"

class CacheMemory : public SimObject
{
  public:
    typedef RubyCacheParams Params;
    CacheMemory(const Params *p);
    ~CacheMemory();

    void init();

    // Public Methods
    void printConfig(std::ostream& out);

    // perform a cache access and see if we hit or not.  Return true on a hit.
    bool tryCacheAccess(const Address& address, CacheRequestType type,
                        DataBlock*& data_ptr);

    // similar to above, but doesn't require full access check
    bool testCacheAccess(const Address& address, CacheRequestType type,
                         DataBlock*& data_ptr);

    // tests to see if an address is present in the cache
    bool isTagPresent(const Address& address) const;

    // Returns true if there is:
    //   a) a tag match on this address or there is
    //   b) an unused line in the same cache "way"
    bool cacheAvail(const Address& address) const;

    // find an unused entry and sets the tag appropriate for the address
    void allocate(const Address& address, AbstractCacheEntry* new_entry);

    // Explicitly free up this address
    void deallocate(const Address& address);

    // Returns with the physical address of the conflicting cache line
    Address cacheProbe(const Address& address) const;

    // looks an address up in the cache
    AbstractCacheEntry& lookup(const Address& address);
    const AbstractCacheEntry& lookup(const Address& address) const;

    // Get/Set permission of cache block
    AccessPermission getPermission(const Address& address) const;
    void changePermission(const Address& address, AccessPermission new_perm);

    int getLatency() const { return m_latency; }

    // Hook for checkpointing the contents of the cache
    void recordCacheContents(CacheRecorder& tr) const;
    void
    setAsInstructionCache(bool is_icache)
    {
        m_is_instruction_only_cache = is_icache;
    }

    // Set this address to most recently used
    void setMRU(const Address& address);

    void profileMiss(const CacheMsg & msg);

    void getMemoryValue(const Address& addr, char* value,
                        unsigned int size_in_bytes);
    void setMemoryValue(const Address& addr, char* value,
                        unsigned int size_in_bytes);

    void setLocked (const Address& addr, int context);
    void clearLocked (const Address& addr);
    bool isLocked (const Address& addr, int context);
    // Print cache contents
    void print(std::ostream& out) const;
    void printData(std::ostream& out) const;

    void clearStats() const;
    void printStats(std::ostream& out) const;

  private:
    // convert a Address to its location in the cache
    Index addressToCacheSet(const Address& address) const;

    // Given a cache tag: returns the index of the tag in a set.
    // returns -1 if the tag is not found.
    int findTagInSet(Index line, const Address& tag) const;
    int findTagInSetIgnorePermissions(Index cacheSet,
                                      const Address& tag) const;

    // Private copy constructor and assignment operator
    CacheMemory(const CacheMemory& obj);
    CacheMemory& operator=(const CacheMemory& obj);

  private:
    const std::string m_cache_name;
    int m_latency;

    // Data Members (m_prefix)
    bool m_is_instruction_only_cache;
    bool m_is_data_only_cache;

    // The first index is the # of cache lines.
    // The second index is the the amount associativity.
    m5::hash_map<Address, int> m_tag_index;
    std::vector<std::vector<AbstractCacheEntry*> > m_cache;
    std::vector<std::vector<int> > m_locked;

    AbstractReplacementPolicy *m_replacementPolicy_ptr;

    CacheProfiler* m_profiler_ptr;

    int m_cache_size;
    std::string m_policy;
    int m_cache_num_sets;
    int m_cache_num_set_bits;
    int m_cache_assoc;
};

#endif // __MEM_RUBY_SYSTEM_CACHEMEMORY_HH__

