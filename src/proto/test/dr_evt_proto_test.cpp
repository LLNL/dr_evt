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
 * This code reads a single prototext input that conforms to the schema
 * `src/proto/dr_evt_params.proto`. The input file can be either text or
 * binary. This compiles independently of DR_EVT.
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

bool read_prototext(const std::string& file_name,
                    const bool is_binary,
                    dr_evt_proto::DR_EVT_Params& dr_evt_proto_params)
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
            std::cerr << "Failed to parse DR_EVT_Params "
                         "in binary-formatted input file: "
                      << file_name << std::endl;
            return false;
        }
    } else {
        google::protobuf::io::IstreamInputStream istrm(&input);
        if (!google::protobuf::TextFormat::Parse(&istrm, &dr_evt_proto_params)) {
            std::cerr << "Failed to parse DR_EVT_Params "
                         "in text-formatted input file: "
                      << file_name << std::endl;
            return false;
        }
    }
    return true;
}

} // end of namespace dr_evt

int main(int argc, char** argv)
{
    if ((argc < 2) || (argc > 3)) {
        std::cout << "Usage: " << argv[0] << " prototext_file is_binary[0|1]"
                  << std::endl;
        return EXIT_SUCCESS;
    }
    std::string prototext = argv[1];
    const bool is_binary = ((argc == 3)? static_cast<bool>(argv[2]) : false);
    dr_evt_proto::DR_EVT_Params dr_evt_proto_params;

    dr_evt::read_prototext(prototext, is_binary, dr_evt_proto_params);

    std::string str;
    google::protobuf::TextFormat::PrintToString(dr_evt_proto_params, &str);
    std::cout << str;

    google::protobuf::ShutdownProtobufLibrary();

    return EXIT_SUCCESS;
}
