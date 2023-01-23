# Add the source files for this directory
set_full_path(THIS_DIR_HEADERS
    boost.hpp
    common.hpp
    dr_evt_types.hpp
)

set_full_path(THIS_DIR_SOURCES
)

# Add the subdirectories
add_subdirectory(params)
add_subdirectory(proto)
add_subdirectory(sim)
add_subdirectory(trace)
add_subdirectory(utils)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
