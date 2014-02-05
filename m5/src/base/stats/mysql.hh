/*
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
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

#ifndef __BASE_STATS_MYSQL_HH__
#define __BASE_STATS_MYSQL_HH__

#include <map>
#include <string>

#include "base/stats/output.hh"
#include "config/use_mysql.hh"

namespace MySQL { class Connection; }
namespace Stats {

class DistInfo;
class MySqlRun;

struct SetupStat
{
    std::string name;
    std::string descr;
    std::string type;
    bool print;
    uint16_t prereq;
    int8_t prec;
    bool nozero;
    bool nonan;
    bool total;
    bool pdf;
    bool cdf;

    double min;
    double max;
    double bktsize;
    uint16_t size;

    void init();
    unsigned setup(MySqlRun *run);
};

class InsertData
{
  private:
    char *query;
    size_type size;
    bool first;
    static const size_type maxsize = 1024*1024;

  public:
    MySqlRun *run;

  public:
    uint64_t tick;
    double data;
    uint16_t stat;
    int16_t x;
    int16_t y;

  public:
    InsertData(MySqlRun *_run);
    ~InsertData();

    void flush();
    void insert();
};

class MySql : public Output
{
  protected:
    MySqlRun *run; /* Hide the implementation so we don't have a
                      #include mess */

    SetupStat stat;
    InsertData newdata;
    std::list<FormulaInfo *> formulas;
    bool configured;

  protected:
    std::map<int, int> idmap;

    void
    insert(int sim_id, int db_id)
    {
        using namespace std;
        idmap.insert(make_pair(sim_id, db_id));
    }

    int
    find(int sim_id)
    {
        using namespace std;
        map<int,int>::const_iterator i = idmap.find(sim_id);
        assert(i != idmap.end());
        return (*i).second;
    }

  public:
    MySql();
    ~MySql();

    void connect(const std::string &host, const std::string &user,
        const std::string &passwd, const std::string &db,
        const std::string &name, const std::string &sample,
        const std::string &project);
    bool connected() const;

  public:
    // Implement Visit
    virtual void visit(const ScalarInfo &info);
    virtual void visit(const VectorInfo &info);
    virtual void visit(const DistInfo &info);
    virtual void visit(const VectorDistInfo &info);
    virtual void visit(const Vector2dInfo &info);
    virtual void visit(const FormulaInfo &info);

    // Implement Output
    virtual bool valid() const;
    virtual void output();

  protected:
    // Output helper
    void output(const ScalarInfo &info);
    void output(const VectorInfo &info);
    void output(const DistInfo &info);
    void output(const VectorDistInfo &info);
    void output(const Vector2dInfo &info);
    void output(const FormulaInfo &info);
    void output(const DistData &data, const DistParams *params);

    void configure();
    bool configure(const Info &info, std::string type);
    void configure(const ScalarInfo &info);
    void configure(const VectorInfo &info);
    void configure(const DistInfo &info);
    void configure(const VectorDistInfo &info);
    void configure(const Vector2dInfo &info);
    void configure(const FormulaInfo &info);
};

bool initMySQL(std::string host, std::string database, std::string user,
    std::string passwd, std::string project, std::string name,
    std::string sample);

#if !USE_MYSQL
inline bool
initMySQL(std::string host, std::string user, std::string password,
    std::string database, std::string project, std::string name,
    std::string sample)
{
    return false;
}
#endif

/* namespace Stats */ }

#endif // __BASE_STATS_MYSQL_HH__
