/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_UTILS_STATE_IO_CEREAL_HPP
#define DR_EVT_UTILS_STATE_IO_CEREAL_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

#if defined(DR_EVT_HAS_CEREAL)
#include <cereal/archives/binary.hpp>
#include <iostream>
#include "streamvec.hpp"
#include "streambuff.hpp"
#include "traits.hpp" // is_trivially_copyable

namespace dr_evt {
/** \addtogroup dr_evt_utils
 *  @{ */

template <typename T>
constexpr bool is_custom_bin_cerealizable()
{
    return (!std::is_arithmetic<T>::value &&
             std::is_trivially_copyable<T>::value);
}
} // end of namespace dr_evt

#define ENABLE_CUSTOM_CEREAL(T) \
namespace cereal { \
    inline std::enable_if_t<dr_evt::is_custom_bin_cerealizable<T>(), void> \
    CEREAL_SAVE_FUNCTION_NAME(BinaryOutputArchive & ar, T const & t) \
    { \
        ar.saveBinary(std::addressof(t), sizeof(t)); \
    } \
    inline std::enable_if_t<dr_evt::is_custom_bin_cerealizable<T>(), void> \
    CEREAL_LOAD_FUNCTION_NAME(BinaryInputArchive & ar, T & t) \
    { \
        ar.loadBinary(std::addressof(t), sizeof(t)); \
    } \
}

namespace dr_evt {

template <typename T>
void save_state(const T& state, std::ostream& os)
{
    // Create an output archive with the given stream
    cereal::BinaryOutputArchive oarchive(os);

    oarchive(state); // Write the data to the archive
    // archive goes out of scope,
    // ensuring all contents are flushed to the stream
}

template <typename T>
void load_state(T& state, std::istream& is)
{
    // Create an input archive using the given stream
    cereal::BinaryInputArchive iarchive(is);

    iarchive(state); // Read the data from the archive
}

/**@}*/
} // end of namespace dr_evt
#endif // DR_EVT_HAS_CEREAL

#endif // DR_EVT_UTILS_STATE_IO_CEREAL_HPP
