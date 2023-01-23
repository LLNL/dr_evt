/******************************************************************************
 *                                                                            *
 *    Copyright 2023   Lawrence Livermore National Security, LLC and other    *
 *    Whole Cell Simulator Project Developers. See the top-level COPYRIGHT    *
 *    file for details.                                                       *
 *                                                                            *
 *    SPDX-License-Identifier: MIT                                            *
 *                                                                            *
 ******************************************************************************/

#ifndef DR_EVT_COMMON_HPP
#define DR_EVT_COMMON_HPP

#if defined(DR_EVT_HAS_CONFIG)
#include "dr_evt_config.hpp"
#else
#error "no config"
#endif

/**
 *  This will enable to print out only time_limit vs exec_time.
 */
//#define LIMIT_VS_EXEC_TIME_ONLY 1

// The dedicated allocation time (DAT) requires an application in advance
// to gain execlusive access to the entire compute resources of a cluster.

/// Indicate to include DAT jobs that ran through pall queue
#define INCLUDE_DAT 1

#define MARK_DAT_PERIOD 1

// Includes jobs from all the queues rather than pbatch and pall
//#define SHOW_ALL_QUEUE 1

/// group all pbatch[0-3]* as pbatch
#define PBATCH_GROUP 1

/**
 *  Show the record number in the raw file for each record.
 *  The record number starts from 1 and increase by the line.
 */
//#define SHOW_ORG_NO 1

/**
 *  The limit on the amount of node resource a batch job can request on Lassen.
 *  Comment out to avoid applying this filtering. Jobs with such an error will
 *  be ignored.
 */
//#define BATCH_JOB_NODE_LIMIT 256

/**
 * Make sure that job submit time is earlier than the job begin time and that
 * job begin time is earler than job end time. Jobs with such an error will be
 * ignored.
 */
#define EVENT_TIME_ORDER 1

/**
 *  For PST timezone. This information is needed to handle daylight saving time.
 *  If this is not defined, UTC is assumed.
 */
#define DATA_TIMEZONE "PST8PDT"

#if !defined (DATA_TIMEZONE)
#define DATA_TIMEZONE "" // Assume UTC
#endif


#if LIMIT_VS_EXEC_TIME_ONLY
 #ifdef INCLUDE_DAT
  #undef INCLUDE_DAT
 #endif
#endif


#define LESS_OR(_A,_B,_T) (((_A) < (_B)) || (((_A) == (_B)) && (_T)))


namespace dr_evt {
/** \addtogroup dr_evt_global
 *  @{ */

/**@}*/
} // end of namespace dr_evt

#include "dr_evt_types.hpp"

#endif // DR_EVT_COMMON_HPP
