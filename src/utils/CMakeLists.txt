# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
  exception.hpp
  file.hpp
  omp_diagnostics.hpp
  rngen.hpp
  rngen_impl.hpp
  seed.hpp
  state_io.hpp
  state_io_impl.hpp
  state_io_cereal.hpp
  streambuff.hpp
  streambuff_impl.hpp
  streamvec.hpp
  streamvec_impl.hpp
  timer.hpp
  traits.hpp
)

set_full_path(THIS_DIR_SOURCES
  exception.cpp
  file.cpp
  omp_diagnostics.cpp
)

# Add the subdirectories
#if (DR_EVT_WITH_UNIT_TESTING)
  add_subdirectory(unit_tests)
#endif ()

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
