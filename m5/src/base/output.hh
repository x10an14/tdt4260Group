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
 * Authors: Nathan Binkert
 */

#ifndef __BASE_OUTPUT_HH__
#define __BASE_OUTPUT_HH__

#include <ios>
#include <map>
#include <string>

class OutputDirectory
{
  private:
    typedef std::map<std::string, std::ostream *> map_t;

    map_t files;
    std::string dir;

    std::string resolve(const std::string &name) const;

  protected:
    std::ostream *checkForStdio(const std::string &name) const;
    std::ostream *openFile(const std::string &filename,
                        std::ios_base::openmode mode = std::ios::trunc) const;

  public:
    OutputDirectory();
    ~OutputDirectory();

    void setDirectory(const std::string &dir);
    const std::string &directory() const;

    std::ostream *create(const std::string &name, bool binary = false);
    std::ostream *find(const std::string &name);

    static bool isFile(const std::ostream *os);
    static inline bool isFile(const std::ostream &os) { return isFile(&os); }
};

extern OutputDirectory simout;

#endif // __BASE_OUTPUT_HH__
