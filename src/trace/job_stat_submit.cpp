/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <cmath>
#include <cassert>
#include "trace/job_stat_submit.hpp"

namespace dr_evt {

void Job_Stat_Submit::reserve(size_t num_weeks)
{
    m_tsubmit.reserve(num_weeks);
}

num_jobs_t Job_Stat_Submit::get_num_weeks() const
{
    return static_cast<num_jobs_t>(m_tsubmit.size());
}

Job_Stat_Submit::Context::Context(const Trace& trace)
  : m_it_end(trace.get_reserved().cend()),
    m_it(trace.get_reserved().cbegin()),
    m_week_end(get_time_of_cur_week_start(
                    trace.data().at(0).get_submit_time())),
    m_week_start(m_week_end),
    m_ratio(static_cast<avail_t>(1.0)),
    m_dat_continue(false)
{
}

void Job_Stat_Submit::Context::set_availability(tslot_week_t& week)
{
    assert(m_hours.size() == week.size()+1);
    hour_boundaries_of_week(m_week_start, m_hours);

    for (size_t i = 0ul; i < week.size(); ++i) {
        m_slot_start = m_hours[i];
        m_slot_end = m_hours[i+1];
        m_ratio = static_cast<avail_t>(1.0);
        calc_availability();
        week[i].set_availability(m_ratio);
    }
}

void Job_Stat_Submit::Context::advance_week(std::time_t t, tsubmit_t& history)
{
    if (history.empty()) {
        m_week_start = m_week_end;
        m_week_end = get_time_of_next_week_start(t);
    } else {
        tslot_week_t& week = history.back();
        set_availability(week);
        m_week_start = m_week_end;
        m_week_end = get_time_of_next_week_start(t);
    }
}

void Job_Stat_Submit::Context::calc_availability()
{
    if (m_it == m_it_end) return;

    const auto& dat_start = m_it->first.first;
    const auto& dat_end = m_it->second.first;

    if (m_dat_continue)
    {
        if (m_slot_end <= dat_end) {
            m_ratio = static_cast<avail_t>(0.0);
        } else {
            m_ratio -= static_cast<avail_t>(
                std::difftime(dat_end, m_slot_start)/hour_in_sec);
            m_dat_continue = false;
            m_it ++; // Check the next DAT
            calc_availability();
        }
    } else { // !dat_continue
        // A new DAT starts in this slot
        if (dat_start < m_slot_end) {
            // DAT does not end in this slot
            if (m_slot_end <= dat_end) {
                m_ratio -= static_cast<avail_t>(
                    std::difftime(m_slot_end, dat_start)/hour_in_sec);
                m_dat_continue = true;
            } else { // It also ends in this slot
                m_ratio -= static_cast<avail_t>(
                    std::difftime(dat_end, dat_start)/hour_in_sec);
                m_dat_continue = false;
                m_it ++; // Check the next DAT
                calc_availability();
            }
        } // else no more DAT in this slot
    }
}

void Job_Stat_Submit::mark_slots_out_of_trace_period(const Trace& trace)
{
    const auto& jobs = trace.data();
    if (jobs.empty() || m_tsubmit.empty()) {
        return;
    }

    // Time of the first job submitted
    const auto t_first = jobs[0].get_submit_time();
    // Hour-slot index of the first job submitted in the trace
    const auto i_first = get_hour_bin_id(t_first.first);

    auto& week_first = m_tsubmit[0];
    for (auto i = static_cast<hour_bin_id_t>(0u);
         (i < i_first) && (i < week_first.size()) ; ++i)
    {
        week_first[i].clear_availability();
    }
    //std::cout << "Num slots before: " << i_first << std::endl;

    // Time of the last job submitted
    const auto t_last  = jobs.back().get_submit_time();
    // Hour-slot index of the last job submitted in the trace
    const auto i_last  = get_hour_bin_id(t_last.first);

    auto& week_last = m_tsubmit.back();
    for (auto i = i_last + 1; i < week_last.size(); ++i) {
        week_last[i].clear_availability();
    }
    //std::cout << "Num slots after: " << week_last.size() - i_last << std::endl;
}


void Job_Stat_Submit::process(const Trace& trace)
{
    const auto& jobs = trace.data();
    if (jobs.empty()) {
        return;
    }

    // A rough estimation of how many weeks in the trace
    const auto num_weeks = static_cast<size_t>(
        std::ceil(std::ceil(jobs.back().get_end_time() -
                              jobs.front().get_submit_time())
                   / Context::week_in_sec));
    reserve(num_weeks+1u);
    Context ctx(trace);

    for (const auto& job: jobs) {
        // Check if job is a batch job
        if (!_Is_Batch(job.get_queue())) {
            continue;
        }
        const auto ts = job.get_submit_time();
        if (ts.first >= ctx.week_end()) {
            // Update the window of week of interest
            ctx.advance_week(ts.first, m_tsubmit);
            m_tsubmit.push_back({});
        }
        const auto hour_idx = get_hour_bin_id(ts.first);
        auto& slot = m_tsubmit.back()[hour_idx];
        slot.inc();
    }
    if (!m_tsubmit.empty()) {
        ctx.set_availability(m_tsubmit.back()); // for the last week
        mark_slots_out_of_trace_period(trace);
    }
}

std::ostream& Job_Stat_Submit::print(std::ostream& os) const
{
    size_t cnt = 0ul;
    for (size_t i = 0ul; i < m_tsubmit.size(); ++i) {
        std::string str;
        const auto& week = m_tsubmit[i];
        for (size_t j = 0ul; j < week.size(); ++j, ++cnt) {
            const auto& hour = week[j];
            str += std::to_string(cnt) + '\t'
                 + std::to_string(hour.num_jobs()) + '\t'
                 + std::to_string(hour.availability()) + '\n';
        }
        os << str;
    }
    return os;
}

Job_Stat_Submit::Summary::Summary()
  : m_tot_submit(static_cast<num_jobs_t>(0u)),
    m_num_blocked(static_cast<num_jobs_t>(0u)),
    m_min_submit(static_cast<num_jobs_t>(0u)),
    m_max_submit(static_cast<num_jobs_t>(0u)),
    m_avg_submit(static_cast<Summary::avg_submit_t>(0.0)),
    m_avg_submit_unscaled(static_cast<Summary::avg_submit_t>(0.0)),
    m_avg_avail(static_cast<avail_t>(0.0)),
    m_std_submit(static_cast<Summary::avg_submit_t>(0.0)),
    m_std_avail(static_cast<avail_t>(0.0))
{}

std::string Job_Stat_Submit::Summary::header_str()
{
    return "hr\ttot_sub\tnum_blocked\tmin_sub\tmax_sub"
           "\tavg_sub\tstd_sub\tavg_sub_us\tavg_avail\tstd_avail\n";
}

std::string Job_Stat_Submit::Summary::to_string() const
{
    using std::to_string;
    return to_string(m_tot_submit) + '\t'
         + to_string(m_num_blocked) + '\t'
         + to_string(m_min_submit) + '\t'
         + to_string(m_max_submit) + '\t'
         + to_string(m_avg_submit) + '\t'
         + to_string(m_std_submit) + '\t'
         + to_string(m_avg_submit_unscaled) + '\t'
         + to_string(m_avg_avail) + '\t'
         + to_string(m_std_avail) + '\n';
}

Job_Stat_Submit::summary_week_t Job_Stat_Submit::get_summary() const
{
    summary_week_t summary;

    if (m_tsubmit.empty()) {
        return summary;
    }

    const size_t num_weeks = m_tsubmit.size();

    // sum for average
    for (size_t i = 0ul; i < num_weeks; ++i) {
        const auto& week = m_tsubmit[i];
        size_t j = 0ul;
        for (size_t wd = 0ul; wd < 7ul; ++wd) {
            for (size_t hr = 0ul; hr < 24ul; ++hr) {
                auto& sum = summary[j];
                const auto& slot = week[j++];
                const auto nj = slot.num_jobs();
                const auto av = slot.availability();
                sum.m_tot_submit += nj;

                if (av == static_cast<avail_t>(0.0)) {
                    sum.m_num_blocked ++;
                } else {
                    sum.m_avg_submit += static_cast<Summary::avg_submit_t>(
                        static_cast<double>(nj) / av);
                    sum.m_avg_avail += av;
                    sum.m_min_submit = std::min(nj, sum.m_min_submit);
                    sum.m_max_submit = std::max(nj, sum.m_max_submit);
                }
            }
        }
    }

    // division for average
    for (size_t s = 0ul; s < summary.size(); ++s) {
        auto& sum = summary[s];
        const auto num_weeks_effective =
            static_cast<num_jobs_t>(num_weeks - sum.m_num_blocked);

        if (num_weeks_effective == static_cast<num_jobs_t>(0u)) {
            sum.m_avg_submit = static_cast<Summary::avg_submit_t>(0.0);
        } else {
            sum.m_avg_submit = static_cast<Summary::avg_submit_t>(
                sum.m_avg_submit / static_cast<double>(num_weeks_effective));
        }

        sum.m_avg_submit_unscaled = static_cast<Summary::avg_submit_t>(
            static_cast<double>(sum.m_tot_submit) /
            static_cast<double>(num_weeks));
        sum.m_avg_avail = static_cast<avail_t>(
            sum.m_avg_avail / static_cast<double>(num_weeks));
    }

    // sum of square of differences for std
    for (size_t i = 0ul; i < num_weeks; ++i) {
        const auto& week = m_tsubmit[i];
        size_t j = 0ul;
        for (size_t wd = 0ul; wd < 7ul; ++wd) {
            for (size_t hr = 0ul; hr < 24ul; ++hr) {
                auto& sum = summary[j];
                const auto& slot = week[j++];
                const auto nj = slot.num_jobs() ;
                const auto av = slot.availability();

                if (av == static_cast<avail_t>(0.0)) {
                    sum.m_std_avail += sum.m_avg_avail * sum.m_avg_avail;
                } else { // In case of submit statistics, using
                    // the effective number of weeks
                    const auto d_s = static_cast<Summary::avg_submit_t>(
                        static_cast<double>(nj) / av - sum.m_avg_submit);
                    sum.m_std_submit += d_s * d_s;
                    const auto d_a = av - sum.m_avg_avail;
                    sum.m_std_avail += d_a * d_a;
                }
            }
        }
    }

    // division for std
    for (size_t s = 0ul; s < summary.size(); ++s) {
        auto& sum = summary[s];
        const auto num_weeks_effective =
            static_cast<num_jobs_t>(num_weeks - sum.m_num_blocked);

        if (num_weeks_effective == static_cast<num_jobs_t>(0u)) {
            sum.m_std_submit = static_cast<Summary::avg_submit_t>(0.0);
        } else {
            sum.m_std_submit = static_cast<Summary::avg_submit_t>(
                std::sqrt(sum.m_std_submit / static_cast<double>(num_weeks_effective)));
        }

        sum.m_std_avail = static_cast<avail_t>(
            std::sqrt(sum.m_std_avail / static_cast<double>(num_weeks)));
    }

    return summary;
}

std::ostream& Job_Stat_Submit::print_summary(std::ostream& os) const
{
    const auto& summary = get_summary();
    print_summary(os, summary);
    return os;
}

std::ostream& Job_Stat_Submit::print_summary(
    std::ostream& os, const summary_week_t& summary) const
{
    os << Summary::header_str();

    for (size_t s = 0ul; s < summary.size(); ++s) {
        auto& sum = summary[s];
        os << std::to_string(s) + '\t' + sum.to_string();
    }
    return os;
}

std::ostream& Job_Stat_Submit::export_submit_data(
    std::ostream& os,
    const bool scale_by_avail) const
{
    using std::to_string;
    const size_t num_weeks = m_tsubmit.size();

    if (scale_by_avail) {
        for (size_t i = 0ul; i < num_weeks; ++i) {
            const auto& week = m_tsubmit[i];
            size_t j = 0ul;
            std::string str = to_string(i); // week no.
            for (size_t wd = 0ul; wd < 7ul; ++wd) {
                for (size_t hr = 0ul; hr < 24ul; ++hr) {
                    const auto& slot = week[j++];
                    const auto nj = slot.num_jobs();
                    const auto av = slot.availability();
                    str += '\t' + to_string(static_cast<num_jobs_t>(
                                             static_cast<double>(nj) / av + 0.5));
                }
            }
            os << str + '\n';
        }
    } else {
        for (size_t i = 0ul; i < num_weeks; ++i) {
            const auto& week = m_tsubmit[i];
            size_t j = 0ul;
            std::string str = to_string(i); // week no.
            for (size_t wd = 0ul; wd < 7ul; ++wd) {
                for (size_t hr = 0ul; hr < 24ul; ++hr) {
                    const auto& slot = week[j++];
                    const auto nj = slot.num_jobs();
                    str += '\t' + to_string(nj);
                }
            }
            os << str + '\n';
        }
    }
    return os;
}

submit_week_t Job_Stat_Submit::export_submit_data(
    const bool scale_by_avail) const
{
    submit_week_t num_submits;
    using std::to_string;
    const size_t num_weeks = m_tsubmit.size();

    for (auto& ns: num_submits) {
        ns.reserve(num_weeks+1u);
        ns.resize(num_weeks, static_cast<num_jobs_t>(0u));
    }

    if (scale_by_avail) {
        for (size_t i = 0ul; i < num_weeks; ++i) {
            const auto& week = m_tsubmit[i];
            size_t j = 0ul;
            std::string str = to_string(i); // week no.
            for (size_t wd = 0ul; wd < 7ul; ++wd) {
                for (size_t hr = 0ul; hr < 24ul; ++hr) {
                    auto& n_sub = num_submits[j][i];
                    const auto& slot = week[j++];
                    const auto nj = slot.num_jobs();
                    const auto av = slot.availability();
                    n_sub = static_cast<num_jobs_t>(static_cast<double>(nj) / av + 0.5);
                }
            }
        }
    } else {
        for (size_t i = 0ul; i < num_weeks; ++i) {
            const auto& week = m_tsubmit[i];
            size_t j = 0ul;
            std::string str = to_string(i); // week no.
            for (size_t wd = 0ul; wd < 7ul; ++wd) {
                for (size_t hr = 0ul; hr < 24ul; ++hr) {
                    auto& n_sub = num_submits[j][i];
                    n_sub = week[j++].num_jobs();
                }
            }
        }
    }
    return num_submits;
}

} // end of namespace dr_evt
