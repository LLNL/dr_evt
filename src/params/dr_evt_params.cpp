/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#include <boost/program_options.hpp>
#include <iostream>
#include <iterator>
#include <string>
#include "dr_evt_params.hpp"

namespace po = boost::program_options;

namespace dr_evt {


bool cmd_line_opts::parse_cmd_line(int argc, char** argv)
{
    try {
        po::positional_options_description pdesc;
        pdesc.add("input-model", 1);

        std::string usage
            = std::string("Usage: ") + argv[0] + " [options] input-model\n"
            + "Allowed options";
        po::options_description desc(usage);
        desc.add_options()
            ("help", "Show the help message.")
            ("setup-all", po::value<std::string>(),
             "Specify the merged prototext file for all setups.")
            ("setup-sim", po::value<std::string>(),
             "Specify the prototext file for simulation setup.")
            ("setup-trace", po::value<std::string>(),
             "Specify the prototext file for tracing setup.")
            ;

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).
                  options(desc).positional(pdesc).run(), vm);

        po::notify(vm);

        m_is_set = false;

        if (vm.size() == 0u || vm.count("help")) {
            std::cout << desc << std::endl;
            return true;
        }

        if (vm.count("setup-all")) {
            m_all_setup = vm["setup-all"].as<std::string>();
            m_is_set = true;
            std::cout << "Merged setup file: "
                      << m_all_setup << std::endl;
            return true;
        }

        if (vm.count("setup-sim")) {
            m_sim_setup = vm["setup-sim"].as<std::string>();
            m_is_set = true;
            std::cout << "Simulation setup file: "
                      << m_sim_setup << std::endl;
        }
        if (vm.count("setup-trace")) {
            m_trace_setup = vm["setup-trace"].as<std::string>();
            m_is_set = true;
            std::cout << "Tracing setup file: "
                      << m_trace_setup << std::endl;
        }
        if (!m_all_setup.empty()) {
            m_sim_setup.clear();
            m_trace_setup.clear();
            m_is_set = true;
        }
    } catch(std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    } catch(...) {
        std::cerr << "Unknown exception!" << std::endl;
    }

    return true;
}

void cmd_line_opts::show() const
{
    std::string msg;
    msg = "Command line options used:";

    if (m_all_setup.empty()) {
        msg += "\n - simulation setup: " + m_sim_setup;
        msg += "\n - tracing setup: " + m_trace_setup;
    } else {
        msg += "\n - all setup: " + m_all_setup;
    }

    std::cout << msg << std::endl << std::endl;
}

} // end of namespace dr_evt

#if 0 // For testing
int main(int argc, char** argv)
{
    dr_evt::cmd_line_opts cmd;
    bool ok = cmd.parse_cmd_line(argc, argv);
    if (!ok) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
#endif
