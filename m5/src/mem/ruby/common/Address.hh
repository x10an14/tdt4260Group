/*
 * Copyright (c) 1999 Mark D. Hill and David A. Wood
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

#ifndef __MEM_RUBY_COMMON_ADDRESS_HH__
#define __MEM_RUBY_COMMON_ADDRESS_HH__

#include <iomanip>

#include "base/hashmap.hh"
#include "mem/ruby/common/Global.hh"
#include "mem/ruby/system/MachineID.hh"
#include "mem/ruby/system/NodeID.hh"
#include "mem/ruby/system/System.hh"

const int ADDRESS_WIDTH = 64; // address width in bytes

class Address;
typedef Address PhysAddress;
typedef Address VirtAddress;

class Address
{
  public:
    Address()
        : m_address(0)
    { }

    explicit
    Address(physical_address_t address)
        : m_address(address)
    { }

    Address(const Address& obj);
    Address& operator=(const Address& obj);

    void setAddress(physical_address_t address) { m_address = address; }
    physical_address_t getAddress() const {return m_address;}
    // selects bits inclusive
    physical_address_t bitSelect(int small, int big) const;
    physical_address_t bitRemove(int small, int big) const;
    physical_address_t maskLowOrderBits(int number) const;
    physical_address_t maskHighOrderBits(int number) const;
    physical_address_t shiftLowOrderBits(int number) const;

    physical_address_t
    getLineAddress() const
    {
        return bitSelect(RubySystem::getBlockSizeBits(), ADDRESS_WIDTH);
    }

    physical_address_t
    getOffset() const
    {
        return bitSelect(0, RubySystem::getBlockSizeBits() - 1);
    }

    void
    makeLineAddress()
    {
        m_address = maskLowOrderBits(RubySystem::getBlockSizeBits());
    }

    // returns the next stride address based on line address
    void
    makeNextStrideAddress(int stride)
    {
        m_address = maskLowOrderBits(RubySystem::getBlockSizeBits())
            + RubySystem::getBlockSizeBytes()*stride;
    }

    int getBankSetNum() const;
    int getBankSetDist() const;

    Index memoryModuleIndex() const;

    void print(std::ostream& out) const;
    void output(std::ostream& out) const;
    void input(std::istream& in);

    void
    setOffset(int offset)
    {
        // first, zero out the offset bits
        makeLineAddress();
        m_address |= (physical_address_t) offset;
    }

  private:
    physical_address_t m_address;
};

inline Address
line_address(const Address& addr)
{
    Address temp(addr);
    temp.makeLineAddress();
    return temp;
}

inline bool
operator<(const Address& obj1, const Address& obj2)
{
    return obj1.getAddress() < obj2.getAddress();
}

inline std::ostream&
operator<<(std::ostream& out, const Address& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

inline bool
operator==(const Address& obj1, const Address& obj2)
{
    return (obj1.getAddress() == obj2.getAddress());
}

inline bool
operator!=(const Address& obj1, const Address& obj2)
{
    return (obj1.getAddress() != obj2.getAddress());
}

// rips bits inclusive
inline physical_address_t
Address::bitSelect(int small, int big) const
{
    physical_address_t mask;
    assert((unsigned)big >= (unsigned)small);

    if (big >= ADDRESS_WIDTH - 1) {
        return (m_address >> small);
    } else {
        mask = ~((physical_address_t)~0 << (big + 1));
        // FIXME - this is slow to manipulate a 64-bit number using 32-bits
        physical_address_t partial = (m_address & mask);
        return (partial >> small);
    }
}

// removes bits inclusive
inline physical_address_t
Address::bitRemove(int small, int big) const
{
    physical_address_t mask;
    assert((unsigned)big >= (unsigned)small);

    if (small >= ADDRESS_WIDTH - 1) {
        return m_address;
    } else if (big >= ADDRESS_WIDTH - 1) {
        mask = (physical_address_t)~0 >> small;
        return (m_address & mask);
    } else if (small == 0) {
        mask = (physical_address_t)~0 << big;
        return (m_address & mask);
    } else {
        mask = ~((physical_address_t)~0 << small);
        physical_address_t lower_bits = m_address & mask;
        mask = (physical_address_t)~0 << (big + 1);
        physical_address_t higher_bits = m_address & mask;

        // Shift the valid high bits over the removed section
        higher_bits = higher_bits >> (big - small);
        return (higher_bits | lower_bits);
    }
}

inline physical_address_t
Address::maskLowOrderBits(int number) const
{
  physical_address_t mask;

  if (number >= ADDRESS_WIDTH - 1) {
      mask = ~0;
  } else {
      mask = (physical_address_t)~0 << number;
  }
  return (m_address & mask);
}

inline physical_address_t
Address::maskHighOrderBits(int number) const
{
    physical_address_t mask;

    if (number >= ADDRESS_WIDTH - 1) {
        mask = ~0;
    } else {
        mask = (physical_address_t)~0 >> number;
    }
    return (m_address & mask);
}

inline physical_address_t
Address::shiftLowOrderBits(int number) const
{
    return (m_address >> number);
}

inline integer_t
Address::memoryModuleIndex() const
{
    integer_t index =
        bitSelect(RubySystem::getBlockSizeBits() +
                  RubySystem::getMemorySizeBits(), ADDRESS_WIDTH);
    assert (index >= 0);
    return index;

    // Index indexHighPortion =
    //     address.bitSelect(MEMORY_SIZE_BITS - 1,
    //                       PAGE_SIZE_BITS + NUMBER_OF_MEMORY_MODULE_BITS);
    // Index indexLowPortion =
    //     address.bitSelect(DATA_BLOCK_BITS, PAGE_SIZE_BITS - 1);
    //
    // Index index = indexLowPortion |
    //     (indexHighPortion << (PAGE_SIZE_BITS - DATA_BLOCK_BITS));

    /*
      Round-robin mapping of addresses, at page size granularity

ADDRESS_WIDTH    MEMORY_SIZE_BITS        PAGE_SIZE_BITS  DATA_BLOCK_BITS
  |                    |                       |               |
 \ /                  \ /                     \ /             \ /       0
  -----------------------------------------------------------------------
  |       unused        |xxxxxxxxxxxxxxx|       |xxxxxxxxxxxxxxx|       |
  |                     |xxxxxxxxxxxxxxx|       |xxxxxxxxxxxxxxx|       |
  -----------------------------------------------------------------------
                        indexHighPortion         indexLowPortion
                                        <------->
                               NUMBER_OF_MEMORY_MODULE_BITS
    */
}

inline void
Address::print(std::ostream& out) const
{
    using namespace std;
    out << "[" << hex << "0x" << m_address << "," << " line 0x"
        << maskLowOrderBits(RubySystem::getBlockSizeBits()) << dec << "]"
        << flush;
}

class Address;
namespace __hash_namespace {
template <> struct hash<Address>
{
    size_t
    operator()(const Address &s) const
    {
        return (size_t)s.getAddress();
    }
};
/* namespace __hash_namespace */ }

namespace std {
template <> struct equal_to<Address>
{
    bool
    operator()(const Address& s1, const Address& s2) const
    {
        return s1 == s2;
    }
};
/* namespace std */ }

#endif // __MEM_RUBY_COMMON_ADDRESS_HH__
