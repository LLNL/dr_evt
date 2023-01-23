/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef  DR_EVT_UTILS_EXCEPTION_HPP
#define  DR_EVT_UTILS_EXCEPTION_HPP
#include <string>
#include <iostream>
#include <exception>

#if 0 // Intel compiler 19.1.2 fails to compile this
#define DR_EVT_THROW(_MSG_)                                         \
    do {                                                            \
        throw dr_evt::exception(std::string( __FILE__) + " : line " \
                              + std::to_string(__LINE__) + " : "    \
                              + _MSG_ + '\n');                      \
    } while (0)
#else
#define DR_EVT_THROW(_MSG_)                                     \
    throw dr_evt::exception(std::string( __FILE__) + " : line " \
                         + std::to_string(__LINE__) + " : "     \
                         + _MSG_ + '\n')
#endif

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

class exception : public std::exception {
  public:
    exception(const std::string message = "");
    const char* what() const noexcept override;

  private:
    std::string m_message;
};

using exception = ::dr_evt::exception;

std::ostream& operator<<(std::ostream& os, const exception& e);

/**@}*/
} // end of namespace dr_evt
#endif //  DR_EVT_UTILS_EXCEPTION_HPP
