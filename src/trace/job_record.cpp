/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <iostream>
#include <stdexcept>
#include "trace/job_record.hpp"
#include "trace/parse_utils.hpp"

namespace dr_evt {

unsigned int Job_Record::num_inputs = 0u;

Job_Record::Job_Record(const Job_Record& o)
  : m_t_begin(o.m_t_begin),
    m_t_end(o.m_t_end),
    m_t_submit(o.m_t_submit),
    m_t_limit(o.m_t_limit),
    m_num_nodes(o.m_num_nodes),
    m_q(o.m_q),
  #if SHOW_ORG_NO
    m_org_no(o.m_org_no),
  #endif
  #if MARK_DAT_PERIOD
    m_dat(o.m_dat),
  #endif
    m_busy_nodes(o.m_busy_nodes)
{}

Job_Record::Job_Record(Job_Record&& o) noexcept
  : m_t_begin(std::move(o.m_t_begin)),
    m_t_end(std::move(o.m_t_end)),
    m_t_submit(std::move(o.m_t_submit)),
    m_t_limit(std::move(o.m_t_limit)),
    m_num_nodes(std::move(o.m_num_nodes)),
    m_q(std::move(o.m_q)),
  #if SHOW_ORG_NO
    m_org_no(std::move(o.m_org_no)),
  #endif
  #if MARK_DAT_PERIOD
    m_dat(std::move(o.m_dat)),
  #endif
    m_busy_nodes(std::move(o.m_busy_nodes))
{}

Job_Record& Job_Record::operator=(const Job_Record& o)
{
    if (this != &o) {
        m_t_begin = o.m_t_begin;
        m_t_end = o.m_t_end;
        m_t_submit = o.m_t_submit;
        m_t_limit = o.m_t_limit;
        m_num_nodes = o.m_num_nodes;
        m_q = o.m_q;
      #if SHOW_ORG_NO
        m_org_no = o.m_org_no;
      #endif
      #if MARK_DAT_PERIOD
        m_dat = o.m_dat;
      #endif
        m_busy_nodes = o.m_busy_nodes;
    }
    return *this;
}

Job_Record& Job_Record::operator=(Job_Record&& o) noexcept
{
    if (this != &o) {
        m_t_begin = std::move(o.m_t_begin);
        m_t_end = std::move(o.m_t_end);
        m_t_submit = std::move(o.m_t_submit);
        m_t_limit = std::move(o.m_t_limit);
        m_num_nodes = std::move(o.m_num_nodes);
        m_q = std::move(o.m_q);
      #if SHOW_ORG_NO
        m_org_no = std::move(o.m_org_no);
      #endif
      #if MARK_DAT_PERIOD
        m_dat = std::move(o.m_dat);
      #endif
        m_busy_nodes = std::move(o.m_busy_nodes);
    }
    return *this;
}

#if SHOW_ORG_NO
Job_Record::Job_Record(job_no_t no, const std::vector<std::string>& str_vec)
  : m_org_no(no),
#else
Job_Record::Job_Record(const std::vector<std::string>& str_vec)
  :
#endif
  #if MARK_DAT_PERIOD
    m_dat(false),
  #endif
    m_busy_nodes(static_cast<num_nodes_t>(0u))
{
    using dr_evt::operator-;
    using dr_evt::operator<;

    if ((num_inputs == 0u) || (str_vec.size() != num_inputs)) {
        throw std::invalid_argument {"Record format does not match! "
          + std::to_string(num_inputs) + " != "
          + std::to_string(str_vec.size())};
        return;
    }

    auto it = str_vec.cbegin();
    set_by(m_num_nodes, *it++);

  #if BATCH_JOB_NODE_LIMIT
    if ((m_num_nodes > BATCH_JOB_NODE_LIMIT) && _Is_Batch(m_q))
    {
        throw std::domain_error
            {"Batch job submitted at " + to_string(m_t_submit) +
             " exceeds the limit of num nodes: " +
             std::to_string(m_num_nodes) + " > " +
             std::to_string(BATCH_JOB_NODE_LIMIT)};
    }
  #endif

    set_by(m_t_begin, *it++);
    set_by(m_t_end, *it++);
    set_by(m_t_submit, *it++);

  #if EVENT_TIME_ORDER
    if ((m_t_begin > m_t_end) || (m_t_submit > m_t_begin)) {
        m_t_submit = convert_time(dr_evt::to_string(m_t_submit));
        m_t_begin  = convert_time(dr_evt::to_string(m_t_begin));
        m_t_end  = convert_time(dr_evt::to_string(m_t_end));

        if ((m_t_begin > m_t_end) || (m_t_submit > m_t_begin)) {
            throw std::domain_error
                {"Job event times are incorrect! " +
                 dr_evt::to_string(m_t_submit) + " < " +
                 dr_evt::to_string(m_t_begin) + " < " +
                 dr_evt::to_string(m_t_end)};
        }
    }
  #endif

    set_by(m_q, *it++);
    set_by(m_t_limit, *it++);
}

std::string Job_Record::get_header_str()
{
    return std::string("num_nodes") + '\t' + "begin_time" + '\t' + "end_time"
      + '\t' + "submit_time" + '\t' + "time_limit" + '\t' + "wait_time"
      + '\t' + "exec_time" + '\t' + "busy_nodes" + '\t' + "queue"
    #if MARK_DAT_PERIOD
      + "\tDAT"
    #endif
    #if SHOW_ORG_NO
      + "\torg_no"
    #endif
      ;
}

std::string Job_Record::to_string() const
{
    using dr_evt::to_string;
    using std::to_string;

  #if MARK_DAT_PERIOD
    static constexpr const char* const dat_str[2] = {"\tNo", "\tYes"};
  #endif

    std::string str =
        to_string(m_num_nodes) + '\t' +
        to_string(m_t_begin) + '\t' +
        to_string(m_t_end) + '\t' +
        to_string(m_t_submit) + '\t' +
        to_string(m_t_limit) + '\t' +
        to_string(get_wait_time()) + '\t' +
        to_string(get_exec_time()) + '\t' +
        to_string(m_busy_nodes) + '\t' +
        to_string(m_q)
      #if MARK_DAT_PERIOD
        + dat_str[m_dat]
      #endif
      #if SHOW_ORG_NO
        + '\t' + to_string(m_org_no)
      #endif
        ;
    return str;
}

std::ostream& operator<<(std::ostream& os, const Job_Record& rec)
{
    os << rec.to_string();

    return os;
}

} // end of namespace dr_evt
