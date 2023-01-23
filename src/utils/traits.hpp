/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_TRAITS_HPP
#define DR_EVT_UTILS_TRAITS_HPP
#include <type_traits>
#include <vector>

#if defined(__GLIBCXX__) && __GLIBCXX__ < 20150801
namespace std {
template <typename T>
struct is_trivially_copyable
  : integral_constant<bool, __has_trivial_copy(T)> {};
}
#endif

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

/// Detect if type T is a vector type
template<typename T>
struct is_vector : public std::false_type {};

/// Detect if type T is a vector type
template<typename T, typename A>
struct is_vector< std::vector<T, A> > : public std::true_type {};

template<typename T, typename A>
struct is_vector<const std::vector<T, A> > : public std::true_type {};

template<typename T, typename A>
struct is_vector<const std::vector<T, A>& > : public std::true_type {};


template<typename T>
struct is_bool
  : std::integral_constant<bool,
      std::is_same<
        typename std::remove_reference<
          typename std::remove_cv<T>::type>::type, bool>::value>
{};

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_UTILS_TRAITS_HPP
