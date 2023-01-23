# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
    column_id.hpp
    data_columns.hpp
    dr_event.hpp
    epoch.hpp
    job_io.hpp
    job_record.hpp
    job_stat_submit.hpp
    job_stat_texec.hpp
    parse_utils.hpp
    trace.hpp
)

set_full_path(THIS_DIR_SOURCES
    data_columns.cpp
    dr_event.cpp
    epoch.cpp
    job_io.cpp
    job_record.cpp
    job_stat_submit.cpp
    parse_utils.cpp
    trace.cpp
)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
