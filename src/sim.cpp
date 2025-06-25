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
#include "proto/dr_evt_params.hpp"
#include "utils/timer.hpp"
#include "trace/trace.hpp"
#include "trace/job_stat_submit.hpp"
#include "sim/sim.hpp"
#include "sim/multi_platform_runs.hpp"
#include "sim/job_submit_model.hpp"

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
    trace.run_job_trace();

    Job_Stat_Submit stat_sub;
    stat_sub.process(trace);
    std::cout << "Number of weeks: " << stat_sub.get_num_weeks () << std::endl;

    return rc;
}


int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;
    dr_evt::Trace_Params cfg_trace;
    dr_evt::Sim_Params cfg_sim;
    // TODO: Use read_proto_params() instead
    //read_proto_params(proto_infile, cfg_sim);
//    if (!cfg_sim.getopt(argc, argv)) {
//        return EXIT_FAILURE;
//    }

    double t_start = dr_evt::get_time();

 //   rc = process_trace(cfg_trace);

    dr_evt::Multi_Platform_Runs<4> mpruns;
    mpruns.reserve (15381u);
    mpruns.load ("../../../DNICHO/runtimes.csv");
    std::ofstream of_mpruns ("out-mprnus.txt");
    mpruns.print (of_mpruns);
    of_mpruns.close ();

    std::cout << "Wall clock time to process trace: "
              << dr_evt::get_time() - t_start << " (sec)" << std::endl;

    return rc;
}
