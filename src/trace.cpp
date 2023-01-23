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
#include "params/trace_params.hpp"
#include "utils/timer.hpp"
#include "trace/trace.hpp"


int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;
    dr_evt::Trace_Params cfg;
    cfg.getopt(argc, argv);

    double t_start = dr_evt::get_time();

    // run the tracing

    std::cout << "Wall clock time to run simulation: "
              << dr_evt::get_time() - t_start << " (sec)" << std::endl;

    return rc;
}
