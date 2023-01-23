/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <string>
#include <iostream>
#include <fstream>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include "utils/file.hpp"
#include "proto/utils.hpp"
#include "proto/dr_evt_params.hpp"

namespace dr_evt {

static void set_sim_options(
    const dr_evt_proto::DR_EVT_Params::Simulation_Params& cfg,
    dr_evt::Sim_Params& sp, bool verbose = false)
{
    //using sim_params = dr_evt_proto::DR_EVT_Params::Simulation_Params;

    sp.m_seed = cfg.seed();

    sp.m_max_jobs = cfg.max_jobs();
    sp.m_max_time = cfg.max_time();

    sp.m_is_jobs_set = (sp.m_max_jobs > 0u);
    sp.m_is_time_set = (sp.m_max_time > 0.0);

    sp.m_infile = cfg.infile();
    sp.set_outfile(cfg.outfile());

    if (!sp.m_is_time_set) {
        sp.m_max_time = dr_evt::max_sim_time;
    }
    if (!sp.m_is_jobs_set && sp.m_is_time_set) {
        sp.m_max_jobs = std::numeric_limits<decltype(sp.m_max_jobs)>::max();
    }

    if (verbose) {
        sp.print();
    }
}

static void set_trace_options(
    const dr_evt_proto::DR_EVT_Params::Tracing_Params& cfg,
    dr_evt::Trace_Params& tp, bool verbose = false)
{
    //using trace_params = dr_evt_proto::DR_EVT_Params::Tracing_Params;

    tp.m_max_jobs = cfg.max_jobs();
    tp.m_max_time = cfg.max_time();

    tp.m_is_jobs_set = (tp.m_max_jobs > 0u);
    tp.m_is_time_set = (! tp.m_max_time.empty());

    tp.m_infile = cfg.infile();
    tp.set_outfile(cfg.outfile());
    tp.m_datfile = cfg.outfile_dat();
    tp.m_subfile = cfg.outfile_sub();
    tp.m_subsumfile = cfg.outfile_subsum();

    if (!tp.m_is_time_set) {
        tp.m_max_time = dr_evt::max_tstamp;
    }
    if (!tp.m_is_jobs_set && tp.m_is_time_set) {
        tp.m_max_jobs = std::numeric_limits<decltype(tp.m_max_jobs)>::max();
    }

    if (verbose) {
        tp.print();
    }
}

void read_proto_params(const std::string& filename,
                       dr_evt::Sim_Params& sp, bool verbose)
{
    dr_evt_proto::DR_EVT_Params::Simulation_Params dr_evt_sim_setup;
    dr_evt::read_prototext(filename, false, dr_evt_sim_setup);

    if (verbose) {
        std::string str;
        google::protobuf::TextFormat::PrintToString(dr_evt_sim_setup, &str);
        std::cout << "---- Prototext '" << filename << "' read ----"
                  << std::endl << str << std::endl;
    }

    set_sim_options(dr_evt_sim_setup, sp, verbose);
}

void read_proto_params(const std::string& filename,
                       dr_evt::Trace_Params& tp, bool verbose)
{
    dr_evt_proto::DR_EVT_Params::Tracing_Params dr_evt_trace_setup;
    dr_evt::read_prototext(filename, false, dr_evt_trace_setup);

    if (verbose) {
        std::string str;
        google::protobuf::TextFormat::PrintToString(dr_evt_trace_setup, &str);
        std::cout << "---- Prototext '" << filename << "' read ----"
                  << std::endl << str << std::endl;
    }

    set_trace_options(dr_evt_trace_setup, tp, verbose);
}

} // end of namespace dr_evt
