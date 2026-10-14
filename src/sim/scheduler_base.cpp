/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

/** @file scheduler_base.cpp
 * @brief Reservation projection and policy-to-scheduler factory implementation.
 */

#include "sim/scheduler_base.hpp"
#include "sim/scheduler_block_fcfs.hpp"
#include "sim/scheduler_circular_fcfs.hpp"
#include "sim/scheduler_fcfs.hpp"
#include "sim/scheduler_fcfs_alt.hpp"
#include "sim/scheduler_fcfs_conservative.hpp"
#include "sim/scheduler_ljf.hpp"
#include "sim/scheduler_sjf.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace dr_evt {

tdiff_t SchedulerBase::prediction_horizon(const running_jobs_t &running_jobs,
                                          sim_time_t current_time,
                                          double utilization) const {
  if (m_backfill_policy != BackfillPolicy::EASY) {
    throw std::logic_error("prediction horizon requires EASY backfilling");
  }
  if (!std::isfinite(utilization) || utilization < 0.0 || utilization > 1.0) {
    throw std::invalid_argument("utilization must be finite and in [0, 1]");
  }

  const auto queued_area = waiting_resource_area();
  if (!queued_area.has_value()) {
    throw std::logic_error(
        "prediction horizon is unsupported by this scheduler");
  }
  if (*queued_area <= 0.0) {
    return 0.0;
  }
  if (m_total_nodes == 0) {
    return std::numeric_limits<tdiff_t>::infinity();
  }

  const double effective_utilization = utilization == 0.0 ? 1.0 : utilization;
  const sim_time_t shadow_time =
      std::max(current_time, m_fcfs_reservation_time);
  double available_nodes = static_cast<double>(m_total_nodes);
  std::map<sim_time_t, num_nodes_t> releases_by_time;
  for (const auto &[job_id, job] : running_jobs) {
    (void)job_id;
    const sim_time_t end_time = job.start_time + job.run_time;
    if (end_time > shadow_time) {
      available_nodes -= static_cast<double>(job.nodes);
      releases_by_time[end_time] += job.nodes;
    }
  }

  tdiff_t usable_area = 0.0;
  sim_time_t previous_time = shadow_time;
  for (const auto &[release_time, nodes_released] : releases_by_time) {
    usable_area += effective_utilization * available_nodes *
                   (release_time - previous_time);
    if (usable_area >= *queued_area) {
      return release_time - shadow_time;
    }
    available_nodes += static_cast<double>(nodes_released);
    previous_time = release_time;
  }

  return previous_time - shadow_time +
         (*queued_area - usable_area) /
             (effective_utilization * static_cast<double>(m_total_nodes));
}

sim_time_t SchedulerBase::calculate_fcfs_reservation(
    num_nodes_t nodes_needed, num_nodes_t free_nodes,
    const running_jobs_t &running_jobs, sim_time_t current_time) {
  if (nodes_needed <= free_nodes) {
    return current_time; // Can start now
  }

  num_nodes_t nodes_deficit = nodes_needed - free_nodes;

  // Collect end times of running jobs with their node counts
  std::vector<std::pair<sim_time_t, num_nodes_t>> end_events;
  end_events.reserve(running_jobs.size());

  for (const auto &[job_idx, job] : running_jobs) {
    (void)job_idx;
    const sim_time_t end_time = job.start_time + job.run_time;

    if (end_time > current_time) {
      end_events.push_back({end_time, job.nodes});
    }
  }

  // Sort by end time
  std::sort(end_events.begin(), end_events.end());

  // Accumulate freed nodes until we have enough
  num_nodes_t freed_nodes = 0;
  for (const auto &[end_time, nodes] : end_events) {
    freed_nodes += nodes;
    if (freed_nodes >= nodes_deficit) {
      return end_time;
    }
  }

  // Not enough nodes will be freed (shouldn't happen in correct usage)
  return current_time;
}

namespace {
/** @brief Return the configuration spelling of a queue implementation.
 * @param[in] impl Queue implementation value.
 * @return Static null-terminated configuration name. */
const char *queue_impl_name(QueueImplementation impl) {
  switch (impl) {
  case QueueImplementation::BLOCK:
    return "block";
  case QueueImplementation::CIRCULAR:
    return "circular";
  case QueueImplementation::MULTIMAP:
    return "multimap";
  case QueueImplementation::DEQUE:
    return "deque";
  default:
    return "multimap";
  }
}
} // anonymous namespace

std::unique_ptr<SchedulerBase>
create_scheduler(num_nodes_t total_nodes, size_t initial_job_count,
                 BackfillPolicy backfill_policy, PriorityPolicy priority_policy,
                 QueueImplementation queue_impl, size_t block_size,
                 size_t wait_queue_capacity,
                 CircularOverflowPolicy wait_queue_overflow) {
  switch (priority_policy) {
  case PriorityPolicy::FCFS:
    // FCFS has 4 queue options
    if (queue_impl == QueueImplementation::BLOCK) {
      // Validate block_size is power of 2
      if (block_size == 0 || (block_size & (block_size - 1)) != 0) {
        std::cerr << "Error: block_size must be a power of 2\n";
        std::exit(1);
      }

      // Compute log2 and use lookup table
      size_t log2_size = 0;
      size_t temp = block_size;
      while (temp > 1) {
        temp >>= 1;
        log2_size++;
      }

      // Verify: (1 << log2_size) == block_size
      if ((1ULL << log2_size) != block_size) {
        std::cerr << "Error: block_size verification failed\n";
        std::exit(1);
      }

      // Dispatch on log2(block_size). This used to be a function-static
      // table of lambdas capturing total_nodes and backfill_policy by
      // reference; the table was built once, so every later call ran the
      // first call's captured references (stack-use-after-return under
      // ASan). A switch has no captured state.
      switch (log2_size) {
      case 2:
        return std::make_unique<BlockQueueFCFSScheduler<4>>(total_nodes,
                                                            backfill_policy);
      case 3:
        return std::make_unique<BlockQueueFCFSScheduler<8>>(total_nodes,
                                                            backfill_policy);
      case 4:
        return std::make_unique<BlockQueueFCFSScheduler<16>>(total_nodes,
                                                             backfill_policy);
      case 5:
        return std::make_unique<BlockQueueFCFSScheduler<32>>(total_nodes,
                                                             backfill_policy);
      case 6:
        return std::make_unique<BlockQueueFCFSScheduler<64>>(total_nodes,
                                                             backfill_policy);
      case 7:
        return std::make_unique<BlockQueueFCFSScheduler<128>>(
            total_nodes, backfill_policy);
      case 8:
        return std::make_unique<BlockQueueFCFSScheduler<256>>(
            total_nodes, backfill_policy);
      default:
        std::cerr << "Error: block_size " << block_size
                  << " not supported. Use 4, 8, 16, 32, 64, 128, or 256.\n";
        std::exit(1);
      }
    } else if (queue_impl == QueueImplementation::MULTIMAP) {
      return std::make_unique<FCFSAltScheduler>(total_nodes, backfill_policy);
    } else if (queue_impl == QueueImplementation::DEQUE) {
      return std::make_unique<FCFSScheduler>(total_nodes, backfill_policy);
    } else if (queue_impl == QueueImplementation::CIRCULAR) {
      return std::make_unique<CircularBufferFCFSScheduler>(
          total_nodes, initial_job_count, backfill_policy, wait_queue_capacity,
          wait_queue_overflow);
    } else {
      // Defensive: QueueImplementation is a 4-value enum and every
      // value is explicitly handled above - this is only reachable
      // if the enum is extended in the future without updating this
      // factory. Throw rather than silently defaulting to any one
      // implementation, so a missed case is caught immediately
      // instead of silently picking the wrong scheduler.
      throw std::runtime_error(
          "create_scheduler: unhandled QueueImplementation value");
    }

  case PriorityPolicy::FCFS_ALT:
    // FCFS_ALT always uses multimap
    if (queue_impl == QueueImplementation::BLOCK ||
        queue_impl == QueueImplementation::DEQUE) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for FCFS_ALT, using multimap\n";
    }
    return std::make_unique<FCFSAltScheduler>(total_nodes, backfill_policy);

  case PriorityPolicy::FCFS_CONSERVATIVE:
    // FCFS with conservative backfilling or no backfilling
    // TODO: Implement CircularBufferFCFSConservativeScheduler for better
    // performance Currently only deque implementation exists
    if (queue_impl != QueueImplementation::DEQUE) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for FCFS_CONSERVATIVE, only deque is "
                   "implemented. Using deque.\n";
    }
    return std::make_unique<FCFSConservativeScheduler>(total_nodes,
                                                       backfill_policy);

  case PriorityPolicy::SJF:
    // SJF only uses multimap (already efficient)
    if (queue_impl != QueueImplementation::CIRCULAR) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for SJF, using default multimap\n";
    }
    return std::make_unique<SJFScheduler>(total_nodes, backfill_policy);

  case PriorityPolicy::LJF:
    // LJF only uses multimap (already efficient)
    if (queue_impl != QueueImplementation::CIRCULAR) {
      std::cerr << "Warning: queue_impl '" << queue_impl_name(queue_impl)
                << "' not supported for LJF, using default multimap\n";
    }
    return std::make_unique<LJFScheduler>(total_nodes, backfill_policy);

  default:
    // Default to FCFS with deque
    return std::make_unique<FCFSScheduler>(total_nodes, backfill_policy);
  }
}

} // namespace dr_evt
