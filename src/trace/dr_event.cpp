/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include "trace/dr_event.hpp"

namespace dr_evt {

DR_Event::DR_Event(const DR_Event& o)
  : m_jidx(o.m_jidx),
    m_t(o.m_t),
    m_type(o.m_type)
{}

DR_Event::DR_Event(DR_Event&& o) noexcept
  : m_jidx(std::move(o.m_jidx)),
    m_t(std::move(o.m_t)),
    m_type(std::move(o.m_type))
{
}

DR_Event& DR_Event::operator=(const DR_Event& o)
{
    if (this != &o) {
        m_jidx = o.m_jidx;
        m_t = o.m_t;
        m_type = o.m_type;
    }
    return *this;
}

DR_Event& DR_Event::operator=(DR_Event&& o) noexcept
{
    if (this != &o) {
        m_jidx = std::move(o.m_jidx);
        m_t = std::move(o.m_t);
        m_type = std::move(o.m_type);
    }
    return *this;
}

DR_Event::DR_Event(job_no_t idx, const epoch_t& t, bool type)
  : m_jidx(idx), m_t(t), m_type(type)
{
}

std::ostream& operator<<(std::ostream& os, const DR_Event& evt)
{
    using std::to_string;
    using dr_evt::to_string;

    std::string msg =
        "job " + to_string(evt.get_job_idx()) + ' ' +
        ((evt.get_type() == arrival)? "\tstarts at " : "\tends at ") +
        to_string(evt.get_time());
    os << msg;
    return os;
}

} // end of namespace dr_evt
