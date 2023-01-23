/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_BOOST_HPP
#define DR_EVT_BOOST_HPP

#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <boost/config.hpp> // for BOOST_LIKELY
#include <type_traits>

// To suppress the gcc compiler warning 'maybe-uninitialized'
// from the boost graph source code.
// clang does not recognize this particular diagnostic flag.
#include <boost/graph/adjacency_list.hpp>
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#endif

namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

template<typename S = void>
struct adjlist_selector_t {
    using type = ::boost::vecS;
    using ordered = std::true_type;
};

template<> struct adjlist_selector_t<::boost::vecS> {
    using type = ::boost::vecS;
    using ordered = std::true_type;
};

template<> struct adjlist_selector_t<::boost::listS> {
    using type = ::boost::listS;
    using ordered = std::true_type;
};

template<> struct adjlist_selector_t<::boost::setS> {
    using type = ::boost::setS;
    using ordered = std::true_type;
};

template<> struct adjlist_selector_t<::boost::multisetS> {
    using type = ::boost::multisetS;
    using ordered = std::true_type;
};

template<> struct adjlist_selector_t<::boost::hash_setS> {
    using type = ::boost::hash_setS;
    using ordered = std::false_type;
};

/**@}*/
} // end of namespace dr_evt

#endif // DR_EVT_BOOST_HPP
