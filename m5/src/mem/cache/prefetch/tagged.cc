/*
 * Copyright (c) 2005 The Regents of The University of Michigan
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
 * Authors: Ron Dreslinski
 */

/**
 * @file
 * Describes a tagged prefetcher based on template policies.
 */

#include "base/trace.hh"
#include "arch/isa_traits.hh"
#include "mem/cache/prefetch/tagged.hh"

TaggedPrefetcher::TaggedPrefetcher(const BaseCacheParams *p)
    : BasePrefetcher(p),
      latency(p->prefetch_latency), degree(p->prefetch_degree),
      useContextId(p->prefetch_use_cpu_id)
{
}

void
TaggedPrefetcher::
calculatePrefetch(PacketPtr &pkt, std::list<Addr> &addresses,
                  std::list<Tick> &delays)
{
    Addr blkAddr = pkt->getAddr() & ~(Addr)(blkSize-1);

    if (useContextId && !pkt->req->hasContextId()) {
        DPRINTF(HWPrefetch, "ignoring request with no context ID\n");
        return;
    }

    for (int d = 1; d <= degree; d++) {
        Addr newAddr = blkAddr + d*(blkSize);
        if (pageStop &&  !samePage(blkAddr, newAddr)) {
            // Spanned the page, so now stop
            pfSpanPage += degree - d + 1;
            return;
        } else {
            addresses.push_back(newAddr);
            delays.push_back(latency);
        }
    }
}
