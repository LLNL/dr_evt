Browse by code file
===================

Open a source file when you know the implementation unit you need. These links
point to the repository's ``main`` branch; use the revision selector there when
reading a release-specific version. For declarations and generated API details,
use the matching source-directory page.

.. raw:: html

   <div class="api-search" role="search">
     <label for="cpp-file-search">Filter code files</label>
     <input id="cpp-file-search" data-api-search="cpp-file-content" type="search" placeholder="e.g., scheduler, trace, params" autocomplete="off">
     <span class="api-search-status" aria-live="polite"></span>
   </div>
   <div id="cpp-file-content" class="api-search-content api-search-map">

Top-level ``src``
-----------------

- `boost.hpp <https://github.com/LLNL/dr_evt/blob/main/src/boost.hpp>`_
- `common.hpp <https://github.com/LLNL/dr_evt/blob/main/src/common.hpp>`_
- `dr_evt_types.hpp <https://github.com/LLNL/dr_evt/blob/main/src/dr_evt_types.hpp>`_
- `sim.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim.cpp>`_
- `trace.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace.cpp>`_

``src/sim``
-----------

- `block_wait_queue.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/block_wait_queue.cpp>`_, `block_wait_queue.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/block_wait_queue.hpp>`_
- `capacity_schedule.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/capacity_schedule.cpp>`_, `capacity_schedule.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/capacity_schedule.hpp>`_
- `job_submit_common.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/job_submit_common.hpp>`_, `job_submit_model.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/job_submit_model.hpp>`_
- `multi_platform_runs.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/multi_platform_runs.hpp>`_
- `schedule_windows.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/schedule_windows.cpp>`_, `schedule_windows.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/schedule_windows.hpp>`_
- `scheduler_base.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_base.cpp>`_, `scheduler_base.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_base.hpp>`_
- `scheduler_block_fcfs.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_block_fcfs.cpp>`_, `scheduler_block_fcfs.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_block_fcfs.hpp>`_
- `scheduler_circular_fcfs.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_circular_fcfs.cpp>`_, `scheduler_circular_fcfs.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_circular_fcfs.hpp>`_
- `scheduler_fcfs.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs.cpp>`_, `scheduler_fcfs.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs.hpp>`_
- `scheduler_fcfs_alt.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_alt.cpp>`_, `scheduler_fcfs_alt.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_alt.hpp>`_
- `scheduler_fcfs_conservative.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_conservative.cpp>`_, `scheduler_fcfs_conservative.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_conservative.hpp>`_
- `scheduler_fcfs_custom.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_custom.cpp>`_, `scheduler_fcfs_custom.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_fcfs_custom.hpp>`_
- `scheduler_ljf.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_ljf.cpp>`_, `scheduler_ljf.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_ljf.hpp>`_
- `scheduler_policies.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_policies.hpp>`_
- `scheduler_sjf.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_sjf.cpp>`_, `scheduler_sjf.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/scheduler_sjf.hpp>`_
- `sim.cpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/sim.cpp>`_, `sim.hpp <https://github.com/LLNL/dr_evt/blob/main/src/sim/sim.hpp>`_

``src/trace``
-------------

- `column_id.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/column_id.hpp>`_
- `data_columns.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/data_columns.cpp>`_, `data_columns.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/data_columns.hpp>`_
- `dr_event.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/dr_event.cpp>`_, `dr_event.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/dr_event.hpp>`_
- `epoch.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/epoch.cpp>`_, `epoch.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/epoch.hpp>`_
- `job_io.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_io.cpp>`_, `job_io.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_io.hpp>`_
- `job_record.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_record.cpp>`_, `job_record.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_record.hpp>`_
- `job_stat_submit.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_stat_submit.cpp>`_, `job_stat_submit.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_stat_submit.hpp>`_
- `job_stat_texec.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/job_stat_texec.hpp>`_
- `parse_utils.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/parse_utils.cpp>`_, `parse_utils.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/parse_utils.hpp>`_
- `trace.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/trace.cpp>`_, `trace.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/trace.hpp>`_
- `trace_policy.cpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/trace_policy.cpp>`_, `trace_policy.hpp <https://github.com/LLNL/dr_evt/blob/main/src/trace/trace_policy.hpp>`_

``src/params``
--------------

- `dr_evt_params.cpp <https://github.com/LLNL/dr_evt/blob/main/src/params/dr_evt_params.cpp>`_, `dr_evt_params.hpp <https://github.com/LLNL/dr_evt/blob/main/src/params/dr_evt_params.hpp>`_
- `sim_params.cpp <https://github.com/LLNL/dr_evt/blob/main/src/params/sim_params.cpp>`_, `sim_params.hpp <https://github.com/LLNL/dr_evt/blob/main/src/params/sim_params.hpp>`_
- `trace_params.cpp <https://github.com/LLNL/dr_evt/blob/main/src/params/trace_params.cpp>`_, `trace_params.hpp <https://github.com/LLNL/dr_evt/blob/main/src/params/trace_params.hpp>`_

``src/proto``
-------------

- `dr_evt_client.cpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_client.cpp>`_
- `dr_evt_params.cpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_params.cpp>`_, `dr_evt_params.hpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_params.hpp>`_
- `dr_evt_server.cpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/dr_evt_server.cpp>`_
- `utils.cpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/utils.cpp>`_, `utils.hpp <https://github.com/LLNL/dr_evt/blob/main/src/proto/utils.hpp>`_

``src/mpi``
-----------

- `mpi_job_feeder.cpp <https://github.com/LLNL/dr_evt/blob/main/src/mpi/mpi_job_feeder.cpp>`_

``src/utils``
-------------

- `exception.cpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/exception.cpp>`_, `exception.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/exception.hpp>`_
- `file.cpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/file.cpp>`_, `file.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/file.hpp>`_
- `omp_diagnostics.cpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/omp_diagnostics.cpp>`_, `omp_diagnostics.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/omp_diagnostics.hpp>`_
- `redis_output.cpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/redis_output.cpp>`_, `redis_output.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/redis_output.hpp>`_
- `rngen.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/rngen.hpp>`_, `rngen_impl.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/rngen_impl.hpp>`_
- `seed.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/seed.hpp>`_
- `state_io.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/state_io.hpp>`_, `state_io_ser20.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/state_io_ser20.hpp>`_, `state_io_impl.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/state_io_impl.hpp>`_
- `streambuff.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/streambuff.hpp>`_, `streambuff_impl.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/streambuff_impl.hpp>`_
- `streamvec.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/streamvec.hpp>`_, `streamvec_impl.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/streamvec_impl.hpp>`_
- `system_memory.cpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/system_memory.cpp>`_, `system_memory.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/system_memory.hpp>`_
- `timer.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/timer.hpp>`_, `to_string.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/to_string.hpp>`_, `traits.hpp <https://github.com/LLNL/dr_evt/blob/main/src/utils/traits.hpp>`_

.. raw:: html

   </div>
