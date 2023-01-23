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
#include "params/sim_params.hpp"


namespace dr_evt {

#define OPTIONS "hi:j:o:s:t:"
static const struct option longopts[] = {
    {"help",     no_argument,        0, 'h'},
    {"infile",   required_argument,  0, 'i'},
    {"max_jobs", required_argument,  0, 'j'},
    {"outfile",  required_argument,  0, 'o'},
    {"seed",     required_argument,  0, 's'},
    {"max_time", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};

Sim_Params::Sim_Params()
  : m_seed(0u), m_max_jobs(10u),
    m_max_time(dr_evt::max_sim_time),
    m_is_jobs_set(false),
    m_is_time_set(false)
{}

void Sim_Params::getopt(int& argc, char** &argv)
{
    int c;
    m_is_jobs_set = false;
    m_is_time_set = false;

    while ((c = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
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
            case 's': /* --seed */
                m_seed = static_cast<unsigned>(atoi(optarg));
                break;
            case 't': /* --max_time */
                m_max_time = static_cast<dr_evt::sim_time_t>(std::stod(optarg));
                m_is_time_set = true;
                break;
            default:
                print_usage(argv[0], 1);
                break;
        }
    }

    if (optind != (argc - 1)) {
        print_usage (argv[0], 1);
    }

    m_infile = argv[optind];
    set_outfile(m_outfile);

    if (!m_is_jobs_set && m_is_time_set) {
        m_max_jobs = std::numeric_limits<decltype(m_max_jobs)>::max();
    }
}

void Sim_Params::print_usage(const std::string exec, int code)
{
    std::cerr <<
        "Usage: " << exec << " inputfile\n"
        "    Run a tracing on the input fileto extract statistics.\n"
        "    Then, run a simulation for the given number of\n"
        "    jobs or the duration of time. Initialize the\n"
        "    simulation with a random number seed.\n"
        "\n"
        "  OPTIONS:\n"
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
        "    -s, --seed\n"
        "        Specify the seed for random number generator. Without this,\n"
        "        it will use a value dependent on the current system clock.\n"
        "\n"
        "    -t, --max_time\n"
        "        Specify the upper limit of simulation time to run.\n"
        "\n";
    exit(code);
}

void Sim_Params::print() const
{
    using std::to_string;
    using std::string;
    string msg;
    msg = "------ Sim params ------\n";
    msg += " - seed: " + to_string(m_seed) + "\n";
    msg += " - max_jobs: " + to_string(m_max_jobs) + "\n";
    msg += " - max_time: " + to_string(m_max_time) + "\n";
    msg += " - infile: " + m_infile + "\n";
    msg += " - outfile: " + m_outfile + "\n";
    msg += " - is_jobs_set: " + string{m_is_jobs_set? "true" : "false"} + "\n";
    msg += " - is_time_set: " + string{m_is_time_set? "true" : "false"} + "\n";

    std::cout << msg << std::endl;
}

void Sim_Params::set_outfile(const std::string& ofname)
{
    m_outfile = ofname;
    if (m_outfile.empty()) {
        if (!m_infile.empty()) {
            m_outfile = dr_evt::get_default_ofname_from_ifname(m_infile);
        } else {
            m_outfile = "sim_out.txt";
        }
    }
}

std::string Sim_Params::get_outfile() const
{
    return m_outfile;
}

} // end of namespace dr_evt
