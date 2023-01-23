/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
#define DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
#include <map>
#include <array>
#include <vector>
#include "sim/job_submit_common.hpp"
#include "trace/trace.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_trace
 *  @{ */

/**
 *  Statistics on job submission rate during each hour of a day of a week.
 *  This assumes that the job sequence is presented in the submission time
 *  order.
 */
class Job_Stat_Submit {
  public:
    /// How much of a timeslot was available for batch jobs. [0.0-1.0]
    using avail_t = float;

    /// Store how many jobs have been submitted during a particular timeslot.
    struct Hour_Slot {
        /// Number of jobs submitted during a particular hour-long period
        num_jobs_t m_num_jobs;

        /** The portion of time during which the resources were available to
          * batch jobs (i.e., no DAT reservation or maintenance) */
        avail_t m_avail;

        Hour_Slot()
          : m_num_jobs(static_cast<num_jobs_t>(0u)),
            m_avail(static_cast<avail_t>(1.0)) {}

        num_jobs_t num_jobs() const { return m_num_jobs; }
        avail_t availability() const { return m_avail; }
        void inc() { m_num_jobs ++; }
        void reset_availability() { m_avail = static_cast<avail_t>(1.0); }
        void clear_availability() { m_avail = static_cast<avail_t>(0.0); }
        void set_availability(const avail_t av) { m_avail = av; }
    };


    /// Slot sumamry statistics: average number of job submission and average availability
    struct Summary {
        /// Average number of job submission at a time slot
        using avg_submit_t = float;

        /// Total number of jobs submitted during this slot
        num_jobs_t m_tot_submit;

        /** Number of cases where this slot was completely blocked.
         *  (i.e., availability = 0.0) */
        num_jobs_t m_num_blocked;

        /**
         *  Min number of submission during the slot without including
         *  blocked cases.  */
        num_jobs_t m_min_submit;
        /** Max number of submission during the slot without scaling
         *  for availability */
        num_jobs_t m_max_submit;

        /** Normalized average number of jobs submitted during this slot.
         *  Normalized for full availability. */
        avg_submit_t m_avg_submit;

        /// Unnormalized average number of jobs submitted during this slot
        avg_submit_t m_avg_submit_unscaled;

        /// Average availability of the slot
        avail_t m_avg_avail;

        /** Standard deviation of number of jobs submitted during this slot
         *  with normalized average */
        avg_submit_t m_std_submit;

        /// Standard deviation of the availability of the slot
        avail_t m_std_avail;

        Summary();
        static std::string header_str();
        std::string to_string() const;
    };


    /** Each element represent a specific 1-hour-long timeslot, [0:00, 1:00),
     *  ..., [23:00-24:00], of a day from Monday to Friday of a particular week.
     */
    using tslot_week_t = std::array<Hour_Slot, 7u*24u>;

    /// History of job submission
    using tsubmit_t = std::vector<tslot_week_t>;

    /// Type for hour-slot summary over multiple weeks
    using summary_week_t = std::array<Summary, 7u*24u>;

  protected:
    /// Job submission history to allow statistical analysis
    tsubmit_t m_tsubmit;

    /// Reserve memory space for stats
    void reserve(size_t num_weeks);

    /** Mark the availability of hour-slots before and after the trace perid
     *  as 0.0 */
    void mark_slots_out_of_trace_period(const Trace& trace);

    /// Struct to keep track of temporary variables during processing
    class Context {
      public:
        using reserved_t = dr_evt::Trace::reserved_t;
        constexpr static const unsigned week_in_sec = 7*24*60*60;
        constexpr static const unsigned day_in_sec  = 24*60*60;
        constexpr static const unsigned hour_in_sec = 60*60;
        constexpr static const unsigned min_in_sec  = 60;

      protected:
        /// The end of DAT list iterator
        const reserved_t::const_iterator m_it_end;

        /// The iterator of current/outstanding DAT
        reserved_t::const_iterator m_it;

        /// Start of the week window
        std::time_t m_week_end;

        /// End of the week window
        std::time_t m_week_start;

        /// Resource availability ratio of the current time slot for batch jobs
        avail_t m_ratio;

        /// Whether a DAT continues across a time bin boundary
        bool m_dat_continue;

        /// End of current slot
        std::time_t m_slot_end;

        /// Start of current slot
        std::time_t m_slot_start;

        /// Temporary hourly time slot boundaries
        std::array<std::time_t, 7*24+1> m_hours;

        /**
         *  Calculate resource availability ratio of the current time slot for
         *  batch jobs based on DAT history.
         */
        void calc_availability();

      public:
        Context(const Trace& trace);
        void advance_week(std::time_t t, tsubmit_t& history);
        void set_availability(tslot_week_t& week);
        std::time_t week_end() const { return m_week_end; }
    };

  public:
    void process(const Trace& trace);
    num_jobs_t get_num_weeks() const;
    summary_week_t get_summary() const;
    std::ostream& print(std::ostream& os) const;
    std::ostream& print_summary(std::ostream& os) const;
    std::ostream& print_summary(std::ostream& os,
                                const summary_week_t& summary) const;
    std::ostream& export_submit_data(std::ostream& os,
                                     const bool scale = false) const;
    submit_week_t export_submit_data(const bool scale = false) const;
};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_TRACE_JOB_STAT_SUBMIT_HPP
