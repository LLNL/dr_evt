# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
    job_submit_common.hpp
    job_submit_model.hpp
    multi_platform_runs.hpp
    sim.hpp
)

set_full_path(THIS_DIR_SOURCES
)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
