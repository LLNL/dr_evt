#ifndef DR_EVT_SIM_JOB_SUBMIT_MODEL_HPP
#define DR_EVT_SIM_JOB_SUBMIT_MODEL_HPP
#include <array>
#include <vector>
#include <algorithm>
#include <iostream>
#include "sim/job_submit_common.hpp"

namespace dr_evt {
/** \addtogroup dr_evt_sim
 *  @{ */

class Job_Submit_Model {
  protected:
    /// Submission data samples
    submit_week_t m_samples;

    /// Order the number of submissions in each hour-slot in ascending order
    void make_bins ();

  public:
    Job_Submit_Model (submit_week_t&& samples);
    Job_Submit_Model (const submit_week_t& samples);
    std::ostream& show_bins (std::ostream& os) const;
};

void Job_Submit_Model::make_bins ()
{
    for (auto& hslot: m_samples) {
        if (hslot.empty ()) {
            continue;
        }
        const size_t n = hslot.size () - 1u;
        std::sort (hslot.begin (), hslot.end ());
        hslot.resize (hslot.size () + 1u);

        if (n == 0ul) {
            hslot[1] = hslot[0];
            continue;
        }
        auto half_range = (hslot[1] - hslot[0])/2;

        // lower bound of the range around the current sample
        auto lb = (hslot[0] > half_range)?
                      (hslot[0] - half_range) :
                      static_cast<num_jobs_t> (0u);

        // upper bound of the range around the current sample
        auto ub = (hslot[1] + hslot[0] + 1)/2;

        for (size_t i = 1u; i < n; ++i) {
            hslot[i-1] = lb;
            lb = ub;
            ub = (hslot[i+1] + hslot[i] + 1)/2;
        }
        half_range = (hslot[n] - hslot[n-1])/2;
        hslot[n-1]  = lb;
        lb = ub;
        ub = half_range + hslot[n];
        hslot[n] = lb;
        hslot[n+1] = ub;
    }
}

Job_Submit_Model::Job_Submit_Model (submit_week_t&& samples)
  : m_samples (std::move (samples))
{
    make_bins ();
}

Job_Submit_Model::Job_Submit_Model (const submit_week_t& samples)
  : m_samples (samples)
{
    make_bins ();
}

std::ostream& Job_Submit_Model::show_bins (std::ostream& os) const
{
    unsigned hr = 0u;
    for (auto& hslot: m_samples) {
        os << hr ++ << ':';
        for (auto const w: hslot) {
            os << ' ' << w;
        }
        os << std::endl;
    }

    return os;
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_SIM_JOB_SUBMIT_MODEL_HPP
