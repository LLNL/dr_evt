/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_DR_EVENT_HPP
#define DR_EVT_TRACE_DR_EVENT_HPP

#include <set>
#include "common.hpp"
#include "trace/epoch.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 *  Discrete Resource Event
 */
class DR_Event {
  protected:
    job_no_t m_jidx; ///< Job index
    epoch_t m_t; ///< Event time
    bool m_type; ///< false => departure (end), true => arrival (start)

  public:
    DR_Event(job_no_t idx, const epoch_t& t, bool type);

    DR_Event(const DR_Event& other);
    DR_Event(DR_Event&& other) noexcept;
    DR_Event& operator=(const DR_Event& rhs);
    DR_Event& operator=(DR_Event&& rhs) noexcept;

    job_no_t get_job_idx() const { return m_jidx; }
    const epoch_t& get_time() const { return m_t; }
    bool get_type() const { return m_type; }
    bool is_arrival() const { return (m_type == arrival); }
    bool is_departure() const { return (m_type == departure); }

    friend bool operator<(const DR_Event& e1, const DR_Event& e2);
    friend bool operator==(const DR_Event& e1, const DR_Event& e2);
};

inline bool operator<(const DR_Event& e1, const DR_Event& e2)
{
    return LESS_OR(e1.m_t, e2.m_t,\
                   LESS_OR(e1.m_type, e2.m_type,\
                           (e1.m_jidx < e2.m_jidx)));
}

inline bool operator==(const DR_Event& e1, const DR_Event& e2)
{
    return (e1.m_jidx == e2.m_jidx)
        && (e1.m_type == e2.m_type)
        && (e1.m_t == e2.m_t);
}

std::ostream& operator<<(std::ostream& os, const DR_Event& evt);

using event_q_t = std::set<DR_Event>;

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_DR_EVENT_HPP
