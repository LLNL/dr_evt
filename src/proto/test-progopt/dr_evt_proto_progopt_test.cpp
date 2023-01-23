/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

/*
 * This code reads a single prototext input or a set of upto three prototext
 * that conforms to the schema `src/proto/dr_evt_params.proto`. In case of the
 * former, it includes the input for DR_EVT_Params. In case of the latter, it
 * includes the input for each of Simulation_Params, and Tracing_Params
 */

#include <string>
#include <iostream>
#include <fstream>
#include <tuple>
#include <list>
#include <functional>
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include "proto/dr_evt_params.pb.h"
#include "params/dr_evt_params.hpp"

namespace dr_evt {

void pbuf_log_collector(google::protobuf::LogLevel level,
                        const char* filename,
                        int line,
                        const std::string& message)
{
    std::string errmsg
        = std::to_string(static_cast<int>(level)) + ' ' + std::string{filename}
        + ' ' + std::to_string(line) + ' ' + message;
    std::cerr << errmsg << std::endl;
}

template<typename T>
bool read_prototext(const std::string& file_name,
                    const bool is_binary,
                    T& dr_evt_proto_params)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::ifstream input(file_name, std::ios::in | std::ios::binary);

    google::protobuf::SetLogHandler(pbuf_log_collector);

    if (!input) {
        std::cerr << file_name << ": File not found!" << std::endl;
        return false;
    }
    if (is_binary) {
        if (!dr_evt_proto_params.ParseFromIstream(&input)) {
          std::cerr << "Failed to parse DR_EVT_Params in binary-formatted input file: "
                    << file_name << std::endl;
          return false;
        }
    } else {
        google::protobuf::io::IstreamInputStream istrm(&input);
        if (!google::protobuf::TextFormat::Parse(&istrm, &dr_evt_proto_params)) {
          std::cerr << "Failed to parse DR_EVT_Params in text-formatted input file: "
                    << file_name << std::endl;
          return false;
        }
    }
    return true;
}

} // end of namespace dr_evt

int main(int argc, char** argv)
{
    dr_evt::cmd_line_opts cmd;
    bool ok = cmd.parse_cmd_line(argc, argv);
    if (!ok) return EXIT_FAILURE;
    if (!cmd.m_is_set) return EXIT_SUCCESS;

    cmd.show();

    if (!cmd.m_all_setup.empty()) {
        dr_evt_proto::DR_EVT_Params dr_evt_all_setup;
        dr_evt::read_prototext(cmd.m_all_setup, false, dr_evt_all_setup);

        std::string str;
        google::protobuf::TextFormat::PrintToString(dr_evt_all_setup, &str);
        std::cout << str;
    } else {
        if (!cmd.m_sim_setup.empty()) {
            dr_evt_proto::DR_EVT_Params::Simulation_Params dr_evt_sim_setup;
            dr_evt::read_prototext(cmd.m_sim_setup, false, dr_evt_sim_setup);
            std::string str;
            google::protobuf::TextFormat::PrintToString(dr_evt_sim_setup, &str);
            std::cout << str;
        }
        if (!cmd.m_trace_setup.empty()) {
            dr_evt_proto::DR_EVT_Params::Tracing_Params dr_evt_trace_setup;
            dr_evt::read_prototext(cmd.m_trace_setup, false, dr_evt_trace_setup);
            std::string str;
            google::protobuf::TextFormat::PrintToString(dr_evt_trace_setup, &str);
            std::cout << str;
        }
    }

    google::protobuf::ShutdownProtobufLibrary();

    return EXIT_SUCCESS;
}
