# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
    sim_params.hpp
    trace_params.hpp
    dr_evt_params.hpp
)

set_full_path(THIS_DIR_SOURCES
    sim_params.cpp
    trace_params.cpp
    dr_evt_params.cpp
)

# Add the subdirectories

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
