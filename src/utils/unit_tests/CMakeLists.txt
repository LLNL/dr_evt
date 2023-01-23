# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
)

set_full_path(THIS_DIR_SOURCES
)

# Add the subdirectories

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
