/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <getopt.h>
#include <limits>
#include <string>
#include <iostream>
#include <cstdlib>
#include "utils/file.hpp"
#include "params/trace_params.hpp"


namespace dr_evt {

#define OPTIONS "d:hi:j:o:s:m:t:"
static const struct option longopts[] = {
    {"datfile",  required_argument,  0, 'd'},
    {"help",     no_argument,        0, 'h'},
    {"infile",   required_argument,  0, 'i'},
    {"max_jobs", required_argument,  0, 'j'},
    {"outfile",  required_argument,  0, 'o'},
    {"subfile",  required_argument,  0, 's'},
    {"subsumf",  required_argument,  0, 'm'},
    {"max_time", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};

Trace_Params::Trace_Params()
  : m_max_jobs(10u),
    m_max_time(dr_evt::max_tstamp),
    m_datfile("out-dat.txt"),
    m_subfile("out-stat_submission.txt"),
    m_subsumfile("out-stat_submission_summary.txt"),
    m_is_jobs_set(false),
    m_is_time_set(false)
{}

bool Trace_Params::getopt(int& argc, char** &argv)
{
    int c;
    m_is_jobs_set = false;
    m_is_time_set = false;

    if (argc < 2) {
        print_usage(argv[0], 0);
        return false;
    }

    while ((c = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'd': /* --datfile */
                m_datfile = std::string(optarg);
                break;
            case 'h': /* --help */
                print_usage(argv[0], 0);
                break;
            case 'i': /* --infile */
                m_infile = std::string(optarg);
                break;
            case 'j': /* --max_jobs */
                m_max_jobs = static_cast<dr_evt::num_jobs_t>(atoi(optarg));
                m_is_jobs_set = true;
                break;
            case 'o': /* --outfile */
                m_outfile = std::string(optarg);
                break;
            case 's': /* --subfile */
                m_subfile = std::string(optarg);
                break;
            case 'm': /* --subsumf */
                m_subsumfile = std::string(optarg);
                break;
            case 't': /* --max_time */
                m_max_time = optarg;
                m_is_time_set = true;
                break;
            default:
                print_usage(argv[0], 1);
                return false;
                break;
        }
    }

    if (m_infile.empty() && (optind != (argc - 1))) {
        print_usage(argv[0], 1);
        return false;
    }

    if (optind == (argc - 1)) {
        if (!m_infile.empty()) {
            print_usage(argv[0], 1);
            return false;
        }
        m_infile = argv[optind];
    }
    set_outfile(m_outfile);

    if (!m_is_jobs_set && m_is_time_set) {
        m_max_jobs = std::numeric_limits<decltype(m_max_jobs)>::max();
    }
    return true;
}

void Trace_Params::print_usage(const std::string exec, int code)
{
    std::cerr <<
        "Usage: " << exec << " inputfile\n"
        "    Run tracing on a job history file to extract statistics\n"
        "    upto a specified time or a number of jobs.\n"
        "\n"
        "  OPTIONS:\n"
        "    -d, --datfile\n"
        "        Specify the out file name for DAT sessions detected.\n"
        "\n"
        "    -h, --help\n"
        "        Display this usage information\n"
        "\n"
        "    -i, --infile\n"
        "        Specify the input file name for simulation.\n"
        "\n"
        "    -j, --max_jobs\n"
        "        Specify the maximum number of jobs to run.\n"
        "\n"
        "    -o, --outfile\n"
        "        Specify the output file name for simulation.\n"
        "\n"
        "    -s, --subfile\n"
        "        Specify the output file name for submission stats.\n"
        "\n"
        "    -m, --subsumf\n"
        "        Specify the output file name for submission stats summary.\n"
        "\n"
        "    -t, --max_time\n"
        "        Specify the upper limit of simulation time to run.\n"
        "\n";
    exit(code);
}

void Trace_Params::print() const
{
    using std::to_string;
    using std::string;
    string msg;
    msg = "------ Trace params ------\n";
    msg += " - max_jobs: " + to_string(m_max_jobs) + "\n";
    msg += " - max_time: " + m_max_time + "\n";
    msg += " - infile: " + m_infile + "\n";
    msg += " - outfile: " + m_outfile + "\n";
    msg += " - datfile: " + m_datfile + "\n";
    msg += " - subfile: " + m_subfile + "\n";
    msg += " - subsumf: " + m_subsumfile + "\n";
    msg += " - is_jobs_set: " + string{m_is_jobs_set? "true" : "false"} + "\n";
    msg += " - is_time_set: " + string{m_is_time_set? "true" : "false"} + "\n";

    std::cout << msg << std::endl;
}

void Trace_Params::set_outfile(const std::string& ofname)
{
    m_outfile = ofname;
    if (m_outfile.empty()) {
        if (!m_infile.empty()) {
            m_outfile = dr_evt::get_default_ofname_from_ifname(m_infile);
        } else {
            m_outfile = "trace_out.txt";
        }
    }
}

} // end of namespace dr_evt
