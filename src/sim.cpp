/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

#include "sim/sim.hpp"
#include "params/sim_params.hpp"
#include "utils/timer.hpp"
#include <cstdlib>
#include <iostream>

namespace {

template <typename SimulationType>
void run_simulation(const dr_evt::Sim_Params &cfg) {
  SimulationType sim(cfg);
  sim.run();

  // Write simulated trace
  sim.write_simulated_trace();

  // Write resource trace (if --resource_trace specified or default location)
  std::string resource_file = cfg.get_resource_trace();
  if (resource_file.empty()) {
    // Default: derive from output file
    resource_file = cfg.get_outfile();
    if (!resource_file.empty()) {
      // Replace .csv with _resources.csv
      size_t pos = resource_file.rfind(".csv");
      if (pos != std::string::npos) {
        resource_file = resource_file.substr(0, pos) + "_resources.csv";
      } else {
        resource_file += "_resources.csv";
      }
    }
  }
  if (!resource_file.empty()) {
    sim.write_resource_trace(resource_file);
  }

  // Print statistics
  std::cout << std::endl;
  sim.print_stats(std::cout);
}

} // namespace

/** @brief Run the command-line scheduling simulator.
 * @param[in] argc Number of command-line arguments.
 * @param[in] argv Command-line argument vector.
 * @return `EXIT_SUCCESS` after a completed simulation, or `EXIT_FAILURE` when
 * configuration or simulation raises an exception. */
int main(int argc, char **argv) {
  int rc = EXIT_SUCCESS;
  dr_evt::Sim_Params cfg;
  cfg.getopt(argc, argv);

  // Print configuration
  cfg.print();

  double t_start = dr_evt::get_time();

  try {
    // Create and run simulation
    if (cfg.m_trace_type == dr_evt::TraceType::PCON) {
      run_simulation<dr_evt::PconSimulation>(cfg);
    } else if (cfg.m_trace_type == dr_evt::TraceType::SWF) {
      run_simulation<dr_evt::SwfSimulation>(cfg);
    } else {
      run_simulation<dr_evt::Simulation>(cfg);
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    rc = EXIT_FAILURE;
  }

  std::cout << "\nWall clock time to run simulation: "
            << dr_evt::get_time() - t_start << " (sec)" << std::endl;

  return rc;
}
