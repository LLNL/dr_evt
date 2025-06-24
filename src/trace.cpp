/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "common.hpp"
#include "params/trace_params.hpp"
#include "trace/trace.hpp"
#include "trace/job_stat_submit.hpp"
#include "utils/timer.hpp"

using std::cout;
using std::cerr;
using std::endl;

using namespace dr_evt;


int process_trace(const dr_evt::Trace_Params& cfg)
{
    Trace trace (cfg.get_infile());

    Trace::trace_data_t& data = trace.data();
    const auto max_num_jobs =
        cfg.is_max_jobs_set()?
            cfg.max_num_jobs() :
            static_cast<num_jobs_t>(0u);

    if (max_num_jobs == static_cast<num_jobs_t>(0u))
        data.reserve(static_cast<num_jobs_t>(1467542u));
    else
        data.reserve(max_num_jobs);

    int rc = trace.load_data(max_num_jobs);
    if (rc != EXIT_SUCCESS) {
        std::cerr << "trace loading not successful! : " << rc << std::endl;
        return rc;
    }
    std::cout << trace.data().size() << " jobs have been loaded." << std::endl;
    trace.run_job_trace();

    std::cout << "Trace ";
    trace.print_span(std::cout);

    std::ofstream of_trace(cfg.get_outfile());
    trace.print(of_trace);
    of_trace.close();

  #if MARK_DAT_PERIOD
    std::ofstream of_dat(cfg.get_datfile());
    trace.print_DAT(of_dat);
    of_dat.close();
  #endif

    Job_Stat_Submit stat_sub;
    stat_sub.process(trace);
    std::ofstream of_stat_sub(cfg.get_subfile());
    stat_sub.print(of_stat_sub);
    of_stat_sub.close();
    std::cout << "Number of weeks: " << stat_sub.get_num_weeks () << std::endl;

    const auto sub_summary = stat_sub.get_summary();
    std::ofstream of_sub_summary(cfg.get_subsumfile());
    stat_sub.print_summary(of_sub_summary, sub_summary);
    of_sub_summary.close();

    return rc;
}


int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;
    dr_evt::Trace_Params cfg;
    // TODO: Use read_proto_params() instead
    if (!cfg.getopt(argc, argv)) {
        return EXIT_FAILURE;
    }

    double t_start = dr_evt::get_time();

    rc = process_trace(cfg);

    std::cout << "Wall clock time to process trace: "
              << dr_evt::get_time() - t_start << " (sec)" << std::endl;

    return rc;
}
