cmake_minimum_required(VERSION 3.24)

project(DR_EVT CXX)

# Single-config generators otherwise leave CMAKE_BUILD_TYPE empty, which
# produces an unoptimized build. Use Release by default without affecting an
# explicit selection or multi-config generators such as Visual Studio.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif ()
if (NOT CMAKE_CONFIGURATION_TYPES)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif ()

# Resolve the platform's install layout before any install or RPATH variable
# is used. The cached CMAKE_INSTALL_LIBDIR value is also inherited by
# dependencies added with FetchContent.
include(GNUInstallDirs)

# Prevent in-source builds
if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR
    "In-source build attempted; please clean the CMake cache and then "
    "switch to an out-of-source build, e.g.,\n"
    "rm -rf CMakeCache.txt CMakeFiles/\nmkdir build && "
    "cd build && cmake <options> ..\n")
endif ()

# Add CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
include(DR_EVT_CMakeUtilities)

#FIXME
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (CMAKE_BUILD_TYPE MATCHES Debug)
  set(DR_EVT_DEBUG TRUE)
endif ()

if (NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON)
endif ()

# Convenience defines
string(TOUPPER "${PROJECT_NAME}" UPPER_PROJECT_NAME)
string(TOLOWER "${PROJECT_NAME}" LOWER_PROJECT_NAME)


################################################################


################################################################
# Version setup
################################################################

set(DR_EVT_VERSION_MAJOR 0)
set(DR_EVT_VERSION_MINOR 9)
set(DR_EVT_VERSION_PATCH 0)

# Build with at least C++20; allow newer standards.
if (NOT CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 20)
  set(CMAKE_CXX_STANDARD 20)
endif ()
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

# This will help define some targets later
if (CMAKE_VERSION VERSION_LESS 3.9)
  set(DR_EVT_PUBLIC_LINK_FLAG)
else ()
  set(DR_EVT_PUBLIC_LINK_FLAG "PUBLIC")
endif ()

set(DR_EVT_VERSION "${DR_EVT_VERSION_MAJOR}.${DR_EVT_VERSION_MINOR}.${DR_EVT_VERSION_PATCH}")

# Check to see if we are in a git repo
find_program(__GIT_EXECUTABLE git)
mark_as_advanced(__GIT_EXECUTABLE)
if (__GIT_EXECUTABLE)

  execute_process(
    COMMAND ${__GIT_EXECUTABLE} rev-parse --is-inside-work-tree
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE __BUILDING_FROM_GIT_SOURCES
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  if (__BUILDING_FROM_GIT_SOURCES)
    # Get the git version so that we can embed it into the executable
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} rev-parse --show-toplevel
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE __GIT_TOPLEVEL_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} rev-parse --git-dir
      WORKING_DIRECTORY "${__GIT_TOPLEVEL_DIR}"
      OUTPUT_VARIABLE __GIT_GIT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
      COMMAND ${__GIT_EXECUTABLE} --git-dir "${__GIT_GIT_DIR}" describe
      --abbrev=7 --always --dirty --tags
      WORKING_DIRECTORY "${__GIT_TOPLEVEL_DIR}"
      OUTPUT_VARIABLE __GIT_DESCRIBE_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(DR_EVT_GIT_VERSION "${__GIT_DESCRIBE_VERSION}"
      CACHE STRING "DR_EVT's version string as told by git.")
  endif (__BUILDING_FROM_GIT_SOURCES)
endif (__GIT_EXECUTABLE)

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
  set(DR_EVT_GNU_LINUX TRUE)
endif ()

################################################################
# Options
################################################################

option(DR_EVT_64BIT_CNT "Enable 64-bit species counter. The default is 32-bit." OFF)

# Ser20 provides binary state serialization for RNGen.
option(DR_EVT_WITH_SER20 "Include Ser20" ON)

option(DR_EVT_WITH_OPENMP "Enable OpenMP" OFF)

option(DR_EVT_GPROF
  "Build with gprof instrumentation (-pg); requires static libraries" OFF)

# GNU gprof does not collect useful call-graph or time samples for code in
# shared libraries.  Keep the cached BUILD_SHARED_LIBS preference intact for
# subsequent non-gprof configurations, but make every library created during
# this configure static so the profiled code is linked into the executables.
if (DR_EVT_GPROF AND BUILD_SHARED_LIBS)
  message(STATUS
    "DR_EVT_GPROF=ON requires static libraries; using BUILD_SHARED_LIBS=OFF "
    "for this configuration")
  set(BUILD_SHARED_LIBS OFF)
endif ()

option(DR_EVT_WITH_REDIS
  "Enable Redis output support through redis-plus-plus" OFF)

option(DR_EVT_WITH_UNIT_TESTING
  "Enable the unit testing framework (requires Catch2)" OFF)

option(DR_EVT_DEBUG_PRINT_SUBTARGETS
  "Turn on debugging output of internal target properties." OFF)
mark_as_advanced(DR_EVT_DEBUG_PRINT_SUBTARGETS)

# This option is off by default because non-developers should not use
# this option under normal circumstances.
option(DR_EVT_WARNINGS_AS_ERRORS
  "Build with warnings promoted to errors." OFF)
mark_as_advanced(DR_EVT_WARNINGS_AS_ERRORS)

######################### For clang ###############################
#set(DR_EVT_OUT_EDGE_LIST_TYPE "::boost::vecS"
#  CACHE STRING "The BGL out edge list type to use in DR_EVT")

#  Setup specific to using clang on Livermore Computing (LC) platforms
#
#  Currently, to use clang on Livermore Computing (LC) platforms with
#  the libraries compiled with gcc (e.g. boost), make sure the c++ standard
#  library is compatible. On LC, clang by default is paired with c++ standard
#  library from gcc/4.9.3. To avoid incompatibility issue,
#
#  0) Use the user libraries compiled with the system default compiler.
#     On many of LC platforms, it is currently gcc/4.9.3. On others,
#     it depends on how clang is configure there.
#  1) Make clang use the c++ standard library from the same version of gcc
#     as that used for building user libraries to link.
#     e.g., clang++ --gcc-toolchain=/usr/tce/packages/gcc/gcc-8.1.0/ ...
#  2) Use clang's c++ standard library. Recompile user libraries in the
#     same way as needed.
#     i.e., clang++ --stdlib=libc++ ...
#
#  Choose either `USE_GCC_LIBCXX` for option 1 or `USE_CLANG_LIBCXX` for
#  option 2 if needed. Usually, option 1 works best. Option 0 does not work
#  well especially with Catch2 due to incomplete support in older toolchains.
#  If neither is chosen, the build relies on the system default, which is
#  on LC with `USE_GCC_LIBCXX` on and `GCC_TOOLCHAIN_VER` set to "4.9.3".
#  If both are on, `USE_GCC_LIBCXX` is turned off. When `USE_GCC_LIBCXX`
#  is on, `GCC_TOOLCHAIN_VER` can be set accordingly (e.g., "8.1.0").

option (USE_CLANG_LIBCXX OFF)
option (USE_GCC_LIBCXX ON)

if (USE_CLANG_LIBCXX)
    set (USE_GCC_LIBCXX OFF)
endif (USE_CLANG_LIBCXX)

if (USE_GCC_LIBCXX)
    set(GCC_TOOLCHAIN_VER "4.9.3" CACHE STRING
        "GCC_TOOLCHAIN_VER chosen by the user at CMake configure time")
    set_property(CACHE GCC_TOOLCHAIN_VER PROPERTY STRINGS
                 "4.9.3" "6.1.0" "7.1.0" "7.3.0" "8.1.0" "8.3.1")
endif ()

################################################################
# Initialize build
################################################################

set(CMAKE_SKIP_BUILD_RPATH  FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
     "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif("${isSystemDir}" STREQUAL "-1")

include(ExternalProject)
include(SetupCXX)

################################################################
# Initialize dependencies
################################################################

# Required dependencies

# Setup Protobuf (optional - can be disabled with -DDR_EVT_ENABLE_PROTOBUF=OFF)
option(DR_EVT_ENABLE_PROTOBUF "Enable Protobuf support" OFF)

# Setup gRPC client/server (optional - requires DR_EVT_ENABLE_PROTOBUF=ON,
# since the gRPC service definition is itself a .proto file)
option(DR_EVT_ENABLE_GRPC "Enable gRPC client/server support" OFF)

option(DR_EVT_INSTALL_FETCHED_GRPC
  "Install fetched gRPC, Protobuf, Abseil, and supporting libraries" OFF)

# Select the trace-file queue representation at configure time. Legacy mode
# keeps existing `queue`-name traces compatible; ID mode reads `q_id` only.
option(DR_EVT_LEGACY_QUEUE_INPUT "Read legacy queue-name trace columns instead of q_id" OFF)

if (DR_EVT_ENABLE_GRPC)
  if (NOT DR_EVT_ENABLE_PROTOBUF)
    message(STATUS "DR_EVT_ENABLE_GRPC=ON automatically enables DR_EVT_ENABLE_PROTOBUF")
    set(DR_EVT_ENABLE_PROTOBUF ON CACHE BOOL "Auto-enabled by gRPC" FORCE)
  endif()
  # SetupGRPC.cmake also provides protobuf::libprotobuf/protobuf::protoc
  # and sets DR_EVT_HAS_PROTOBUF itself, so SetupProtobuf.cmake's own
  # discovery/build is skipped here to avoid two separate Protobuf copies.
  include(SetupGRPC)
elseif (DR_EVT_ENABLE_PROTOBUF)
  include(SetupProtobuf)
endif()

if (DR_EVT_HAS_PROTOBUF)
  # Protobuf's own internal logging is redirected via Abseil's
  # absl::LogSink when available - only recent enough Abseil releases
  # have the full API, so only attempt this if the target exists at
  # all (won't on the standalone-Protobuf-without-gRPC path, or with
  # an older gRPC/Abseil pin). Without it, Protobuf's own default
  # logging still reaches stderr, just unformatted.
  #
  # decltype()-only, no LINK_LIBRARIES: Abseil hasn't actually been
  # compiled yet at this configure step on the FetchContent path, so
  # linking against it here cannot work.
  if (TARGET absl::log_sink_registry)
    get_target_property(DR_EVT_ABSLLOGTEST_INCLUDE_DIRS
                         absl::log_sink_registry INTERFACE_INCLUDE_DIRECTORIES)
    if (NOT DR_EVT_ABSLLOGTEST_INCLUDE_DIRS)
      set(DR_EVT_ABSLLOGTEST_INCLUDE_DIRS "")
    endif()

    try_compile(DR_EVT_HAS_ABSL_LOG_SINK "${CMAKE_BINARY_DIR}/temp"
                "${CMAKE_SOURCE_DIR}/cmake/tests/has_absl_log_sink.cpp"
                CMAKE_FLAGS
                  "-DINCLUDE_DIRECTORIES=${DR_EVT_ABSLLOGTEST_INCLUDE_DIRS}"
                  "${CMAKE_CXX_FLAGS}")
    unset(DR_EVT_ABSLLOGTEST_INCLUDE_DIRS)
  else()
    set(DR_EVT_HAS_ABSL_LOG_SINK FALSE)
  endif()
  if (DR_EVT_HAS_ABSL_LOG_SINK)
    message(STATUS "Abseil has the full log_sink API - "
                    "src/proto/utils.cpp uses it to format Protobuf's "
                    "own internal parse diagnostics")
  else()
    message(STATUS "Abseil's full log_sink API is not available "
                    "(too old an Abseil, or built without gRPC) - "
                    "src/proto/utils.cpp's custom log formatting is "
                    "skipped; Protobuf's own default logging still "
                    "reaches stderr")
  endif (DR_EVT_HAS_ABSL_LOG_SINK)
endif (DR_EVT_HAS_PROTOBUF)

# Setup Boost (will use FetchContent if not found)
include(SetupBoost)

message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")

# Setup Python bindings (optional)
include(SetupPybind11)

# This shouldn't be here, but is ok for now. This will occasionally be
# part of another TPL's libraries (e.g., MKL), but it's no
# guarantee. There's no harm including it multiple times.
if (NOT DL_LIBRARY)
  find_library(DL_LIBRARY dl DOC "The dynamic loader library.")
  if (DL_LIBRARY)
    message(STATUS "Found dl: ${DL_LIBRARY}")
  else ()
    message(FATAL_ERROR
      "dl library not found! This is a required library.\n"
      "Please add the path to libdl to CMAKE_LIBRARY_PATH.")
  endif (DL_LIBRARY)
endif ()

# Optional dependencies

if (DR_EVT_WITH_SER20)
  include(SetupSer20)
endif (DR_EVT_WITH_SER20)

if (DR_EVT_WITH_REDIS)
  include(SetupRedisPlusPlus)
endif (DR_EVT_WITH_REDIS)


if (OpenMP_CXX_FOUND)
  message(STATUS "DR_EVT_OMP_MODE value (${DR_EVT_OMP_MODE})")
  set(DR_EVT_HAS_OPENMP TRUE)

  if (NUMA_FOUND)
    set(DR_EVT_HAS_NUMA TRUE)
  endif (NUMA_FOUND)
endif (OpenMP_CXX_FOUND)

if (DR_EVT_WITH_UNIT_TESTING)
  include(SetupCatch2)
  include(CTest)

  # Now that Catch2 has been found, start adding the unit tests
  # subdirectory addtion has been moved to the CMakeList.txt of each
  # upper directory of unit test directories
  #add_subdirectory(src/sim/unit_test)
  #add_subdirectory(src/trace/unit_test)

  # Add this one last
  #add_subdirectory(unit_test)
endif (DR_EVT_WITH_UNIT_TESTING)

# Handle the documentation
#add_subdirectory(docs)

################################################################
# Build DR_EVT
################################################################

cmake_policy(SET CMP0079 NEW)

# Add DR_EVT source files
add_subdirectory(src)

# Debug: print sources
message(STATUS "DR_EVT_SOURCES: ${DR_EVT_SOURCES}")

# Create the DR_EVT library
add_library(dr_evt ${DR_EVT_SOURCES} ${DR_EVT_HEADERS})
target_compile_features(dr_evt PUBLIC cxx_std_20)
if (DR_EVT_HAS_PROTOBUF)
  # protobuf::libprotobuf must be linked, not just add_dependencies'd,
  # so its include directories propagate to dr_evt's own compilation -
  # otherwise dr_evt's protobuf-using code can silently pick up a
  # different, unrelated protobuf install from the compiler's default
  # system include path. protobuf::protoc is just an executable, so
  # add_dependencies is correct there.
  target_link_libraries(dr_evt PUBLIC protobuf::libprotobuf)
  add_dependencies(dr_evt protobuf::protoc)
  if (DR_EVT_HAS_ABSL_LOG_SINK)
    target_link_libraries(dr_evt PUBLIC absl::log_sink absl::log_sink_registry)
  endif (DR_EVT_HAS_ABSL_LOG_SINK)
  if (DR_EVT_HAS_OPENMP)
    add_dependencies(dr_evt OpenMP::OpenMP_CXX)
    if (DR_EVT_HAS_NUMA)
      add_dependencies(dr_evt NUMA::numa)
    endif (DR_EVT_HAS_NUMA)
  endif (DR_EVT_HAS_OPENMP)
endif (DR_EVT_HAS_PROTOBUF)

target_include_directories(dr_evt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(dr_evt SYSTEM PUBLIC
  $<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

# Use the IMPORTED targets when possible.


if (DR_EVT_HAS_SER20)
  target_link_libraries(dr_evt PUBLIC ser20::ser20)
endif (DR_EVT_HAS_SER20)

if (DR_EVT_HAS_REDIS_PLUS_PLUS)
  target_link_libraries(dr_evt PRIVATE DR_EVT::redis_plus_plus)
endif (DR_EVT_HAS_REDIS_PLUS_PLUS)

if (DR_EVT_WITH_UNIT_TESTING)
  target_include_directories(dr_evt PUBLIC
    $<BUILD_INTERFACE:${CATCH2_INCLUDE_DIR}>)
endif (DR_EVT_WITH_UNIT_TESTING)

if (DR_EVT_HAS_PROTOBUF)
  target_link_libraries(dr_evt PUBLIC dr_evt_proto protobuf::libprotobuf)
endif (DR_EVT_HAS_PROTOBUF)

if (DR_EVT_HAS_OPENMP)
  target_link_libraries(dr_evt PUBLIC OpenMP::OpenMP_CXX)
  if (DR_EVT_HAS_NUMA)
    target_link_libraries(dr_evt PUBLIC NUMA::numa)
  endif (DR_EVT_HAS_NUMA)
endif (DR_EVT_HAS_OPENMP)

################################################################
# Add executables
################################################################

# RPATH setup
set(CMAKE_SKIP_BUILD_RPATH  FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
#https://stackoverflow.com/questions/54464929/cmake-install-rpath-use-link-path-without-any-effect
message(STATUS "CMAKE_INSTALL_RPATH = ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
unset (LIBRARY_PATH)

# add executable simulator
add_executable( sim-bin src/sim.cpp )
target_include_directories(sim-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(sim-bin PRIVATE dr_evt)
set_target_properties(sim-bin PROPERTIES OUTPUT_NAME simulator)
set_target_properties(sim-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_EXEC_TARGETS sim-bin)

# add executable tracer
add_executable( trace-bin src/trace.cpp )
target_include_directories(trace-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(trace-bin PRIVATE dr_evt)
set_target_properties(trace-bin PROPERTIES OUTPUT_NAME tracer)
set_target_properties(trace-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_EXEC_TARGETS trace-bin)

# add executables for the gRPC client/server, if enabled
if (DR_EVT_HAS_GRPC)
  add_executable( dr_evt_server-bin src/proto/dr_evt_server.cpp )
  target_include_directories(dr_evt_server-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(dr_evt_server-bin PRIVATE
    dr_evt dr_evt_service_proto)
  set_target_properties(dr_evt_server-bin PROPERTIES OUTPUT_NAME dr_evt_server)
  set_target_properties(dr_evt_server-bin PROPERTIES CMAKE_INSTALL_RPATH
                        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  list(APPEND DR_EVT_EXEC_TARGETS dr_evt_server-bin)

  add_executable( dr_evt_client-bin src/proto/dr_evt_client.cpp )
  target_include_directories(dr_evt_client-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
  # No dependency on dr_evt itself: the client only speaks gRPC to a
  # separately-running server, it never links against the simulation
  # library directly.
  target_link_libraries(dr_evt_client-bin PRIVATE dr_evt_service_proto)
  if (DR_EVT_HAS_REDIS_PLUS_PLUS)
    target_link_libraries(dr_evt_client-bin PRIVATE DR_EVT::redis_plus_plus)
  endif ()
  set_target_properties(dr_evt_client-bin PROPERTIES OUTPUT_NAME dr_evt_client)
  set_target_properties(dr_evt_client-bin PROPERTIES CMAKE_INSTALL_RPATH
                        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  list(APPEND DR_EVT_EXEC_TARGETS dr_evt_client-bin)

  # Permanent gRPC streaming API test: append requests and backfill-window
  # queries.  See tests/test_grpc_streaming_api.cpp.
  add_executable( test_grpc_streaming_api-bin tests/test_grpc_streaming_api.cpp )
  target_include_directories(test_grpc_streaming_api-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>)
  target_link_libraries(test_grpc_streaming_api-bin PRIVATE dr_evt_service_proto)
  set_target_properties(test_grpc_streaming_api-bin PROPERTIES OUTPUT_NAME test_grpc_streaming_api)
  set_target_properties(test_grpc_streaming_api-bin PROPERTIES CMAKE_INSTALL_RPATH
                        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  list(APPEND DR_EVT_UNIT_TEST_TARGETS test_grpc_streaming_api-bin)

  # MPI-aware multi-client/multi-server test harness (optional: only
  # built if MPI is actually available - unlike gRPC/Protobuf, this
  # project has no existing MPI dependency to build on, so this is the
  # first find_package(MPI) call in this project).
  find_package(MPI)
  if (MPI_FOUND)
    add_executable( test_grpc_multi_client_server-bin
      tests/test_grpc_multi_client_server.cpp )
    target_include_directories(test_grpc_multi_client_server-bin PUBLIC
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(test_grpc_multi_client_server-bin PRIVATE
      dr_evt_service_proto MPI::MPI_CXX)
    set_target_properties(test_grpc_multi_client_server-bin PROPERTIES
      OUTPUT_NAME test_grpc_multi_client_server)
    set_target_properties(test_grpc_multi_client_server-bin PROPERTIES
      CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    list(APPEND DR_EVT_UNIT_TEST_TARGETS test_grpc_multi_client_server-bin)
  else()
    message(STATUS "MPI not found - skipping test_grpc_multi_client_server "
                    "(gRPC client/server themselves are unaffected)")
  endif (MPI_FOUND)
endif (DR_EVT_HAS_GRPC)


# Build tests
add_executable( t_state_rngen-bin src/utils/unit_tests/t_state_rngen.cpp )
target_include_directories(t_state_rngen-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_state_rngen-bin PRIVATE dr_evt)
set_target_properties(t_state_rngen-bin PROPERTIES OUTPUT_NAME t_state_rngen)
set_target_properties(t_state_rngen-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state_rngen-bin)

if (DR_EVT_HAS_SER20)
  add_executable( t_state_ser20-bin src/utils/unit_tests/t_state_ser20.cpp )
  target_include_directories(t_state_ser20-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

  target_link_libraries(t_state_ser20-bin PRIVATE dr_evt)
  set_target_properties(t_state_ser20-bin PROPERTIES OUTPUT_NAME t_state_ser20)
  set_target_properties(t_state_ser20-bin PROPERTIES CMAKE_INSTALL_RPATH
                        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

  list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state_ser20-bin)
endif (DR_EVT_HAS_SER20)


add_executable( t_state-bin src/utils/unit_tests/t_state.cpp )

target_include_directories(t_state-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_state-bin PRIVATE dr_evt)
set_target_properties(t_state-bin PROPERTIES OUTPUT_NAME t_state)
set_target_properties(t_state-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state-bin)

add_executable( t_rngen-bin src/utils/unit_tests/t_rngen.cpp )

target_include_directories(t_rngen-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_rngen-bin PRIVATE dr_evt)
set_target_properties(t_rngen-bin PROPERTIES OUTPUT_NAME t_rngen)
set_target_properties(t_rngen-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_rngen-bin)

# Append-job (genuine streaming) API test - covers both append_job()'s
# own insertion behavior (reclaim-before-grow, capacity, validation) and
# submit_job()/advance_to()'s general correctness, driven via append_job()
# rather than a preloaded trace file.
add_executable( test_append_job_api-bin tests/test_append_job_api.cpp )

target_include_directories(test_append_job_api-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(test_append_job_api-bin PRIVATE dr_evt)
set_target_properties(test_append_job_api-bin PROPERTIES OUTPUT_NAME test_append_job_api)
set_target_properties(test_append_job_api-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS test_append_job_api-bin)

if(DR_EVT_HAS_SER20)
  # Exact continuation across queued/running jobs, output append, and
  # incompatible-state rejection.
  add_executable(test_checkpoint_restart-bin
    tests/test_checkpoint_restart.cpp)
  target_include_directories(test_checkpoint_restart-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(test_checkpoint_restart-bin PRIVATE dr_evt)
  set_target_properties(test_checkpoint_restart-bin PROPERTIES
    OUTPUT_NAME test_checkpoint_restart
    CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  list(APPEND DR_EVT_UNIT_TEST_TARGETS test_checkpoint_restart-bin)
endif()

# Custom callback-driven EASY backfill scheduler, resource accounting, and
# prediction-horizon APIs.
add_executable(test_custom_scheduler-bin
  tests/test_custom_scheduler.cpp)
target_include_directories(test_custom_scheduler-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_custom_scheduler-bin PRIVATE dr_evt)
set_target_properties(test_custom_scheduler-bin PROPERTIES
  OUTPUT_NAME test_custom_scheduler
  CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_custom_scheduler-bin)

# Compile-time policy test: verifies inline Pcon job fields and the expanded
# resource-history output without changing the standard record layouts.
add_executable(test_pcon_trace-bin tests/test_pcon_trace.cpp)
target_include_directories(test_pcon_trace-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_pcon_trace-bin PRIVATE dr_evt)
set_target_properties(test_pcon_trace-bin PROPERTIES OUTPUT_NAME test_pcon_trace)
set_target_properties(test_pcon_trace-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_pcon_trace-bin)

# Replay job-store reclamation test: committed-time boundaries, output
# ordering, and all lazy/explicit flush paths for the standard Trace policy
# used by the standalone tracer.
add_executable(test_replay_reclamation-bin tests/test_replay_reclamation.cpp)
target_include_directories(test_replay_reclamation-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_replay_reclamation-bin PRIVATE dr_evt)
set_target_properties(test_replay_reclamation-bin PROPERTIES
  OUTPUT_NAME test_replay_reclamation
  CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_replay_reclamation-bin)

if (DR_EVT_WITH_UNIT_TESTING)
  add_test(
    NAME test_rngen
    COMMAND $<TARGET_FILE:t_rngen-bin> 42)
  add_test(
    NAME test_state_rngen
    COMMAND $<TARGET_FILE:t_state_rngen-bin>)
  if (DR_EVT_HAS_SER20)
    add_test(
      NAME test_state_ser20
      COMMAND $<TARGET_FILE:t_state_ser20-bin>)
    set_tests_properties(test_state_ser20 PROPERTIES
      LABELS "unit;serialization")
  endif ()
  add_test(
    NAME test_trace_type_cli
    COMMAND ${CMAKE_COMMAND}
      -DSIMULATOR=$<TARGET_FILE:sim-bin>
      -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/test_traces/pcon_cli_input.csv
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_trace_type_cli.cmake)
  add_test(
    NAME test_pcon_trace
    COMMAND $<TARGET_FILE:test_pcon_trace-bin>)
  add_test(
    NAME test_replay_reclamation
    COMMAND $<TARGET_FILE:test_replay_reclamation-bin>)
  set_tests_properties(test_rngen test_state_rngen PROPERTIES LABELS "unit;rng")
endif()

# Progressive/multi-file loading test (--infile_list) - see
# tests/test_progressive_load.cpp
add_executable( test_progressive_load-bin tests/test_progressive_load.cpp )

target_include_directories(test_progressive_load-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(test_progressive_load-bin PRIVATE dr_evt)
set_target_properties(test_progressive_load-bin PROPERTIES OUTPUT_NAME test_progressive_load)
set_target_properties(test_progressive_load-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS test_progressive_load-bin)

# Trace queue-input schema test (legacy queue names or q_id, selected at
# configure time) - intentionally separate from progressive loading.
add_executable( test_queue_input-bin tests/test_queue_input.cpp )
target_include_directories(test_queue_input-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_queue_input-bin PRIVATE dr_evt)
set_target_properties(test_queue_input-bin PROPERTIES OUTPUT_NAME test_queue_input)
set_target_properties(test_queue_input-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_queue_input-bin)


# Batch vs Streaming equivalence test
add_executable( test_batch_vs_streaming-bin tests/test_batch_vs_streaming.cpp )

target_include_directories(test_batch_vs_streaming-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(test_batch_vs_streaming-bin PRIVATE dr_evt)
set_target_properties(test_batch_vs_streaming-bin PROPERTIES OUTPUT_NAME test_batch_vs_streaming)
set_target_properties(test_batch_vs_streaming-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS test_batch_vs_streaming-bin)

# Block-based wait queue test (always enabled)
add_executable( test_block_queue-bin tests/test_block_queue.cpp )

target_include_directories(test_block_queue-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(test_block_queue-bin PRIVATE dr_evt)
set_target_properties(test_block_queue-bin PROPERTIES OUTPUT_NAME test_block_queue)
set_target_properties(test_block_queue-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS test_block_queue-bin)

# Time-varying capacity schedule parser validation.
add_executable(test_capacity_schedule-bin tests/test_capacity_schedule.cpp)
target_include_directories(test_capacity_schedule-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_capacity_schedule-bin PRIVATE dr_evt)
set_target_properties(test_capacity_schedule-bin PROPERTIES
  OUTPUT_NAME test_capacity_schedule
  CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_capacity_schedule-bin)

# Native warm-start coverage: boundary classification, capacity transitions,
# statistics, all scheduler policies/queue backends, and differential traces.
add_executable(test_warm_start-bin tests/test_warm_start.cpp)
target_include_directories(test_warm_start-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(test_warm_start-bin PRIVATE dr_evt)
set_target_properties(test_warm_start-bin PROPERTIES
  OUTPUT_NAME test_warm_start
  CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_UNIT_TEST_TARGETS test_warm_start-bin)


# MPI streaming test (requires MPI)
# Only search for MPI for this test - dr_evt library itself has no MPI dependency
find_package(MPI)
if(MPI_CXX_FOUND)
  if(DR_EVT_HAS_SER20)
    add_executable(mpi_performance_dispatch-bin
      experimental/multi-cluster/mpi_performance_dispatch.cpp)
    target_include_directories(mpi_performance_dispatch-bin PUBLIC
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(mpi_performance_dispatch-bin PRIVATE
      dr_evt MPI::MPI_CXX)
    set_target_properties(mpi_performance_dispatch-bin PROPERTIES
      OUTPUT_NAME mpi_performance_dispatch
      CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    list(APPEND DR_EVT_EXEC_TARGETS mpi_performance_dispatch-bin)
  endif()

  add_executable( test_mpi_streaming-bin tests/test_mpi_streaming.cpp )

  target_include_directories(test_mpi_streaming-bin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>
    ${MPI_CXX_INCLUDE_DIRS})

  target_link_libraries(test_mpi_streaming-bin PRIVATE dr_evt ${MPI_CXX_LIBRARIES})
  set_target_properties(test_mpi_streaming-bin PROPERTIES OUTPUT_NAME test_mpi_streaming)
  set_target_properties(test_mpi_streaming-bin PROPERTIES CMAKE_INSTALL_RPATH
                        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

  list(APPEND DR_EVT_UNIT_TEST_TARGETS test_mpi_streaming-bin)
endif()

if(DR_EVT_WITH_UNIT_TESTING)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  add_test(
    NAME test_append_job_api
    COMMAND $<TARGET_FILE:test_append_job_api-bin>)
  if(DR_EVT_HAS_SER20)
    add_test(
      NAME test_checkpoint_restart
      COMMAND $<TARGET_FILE:test_checkpoint_restart-bin>)
    set_tests_properties(test_checkpoint_restart PROPERTIES
      LABELS "unit;native;checkpoint"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RUN_SERIAL TRUE)
  endif()
  add_test(
    NAME test_custom_scheduler
    COMMAND ${CMAKE_COMMAND} -E env
      CUSTOM_SCHEDULER_TEST=$<TARGET_FILE:test_custom_scheduler-bin>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_custom_scheduler_tests.sh)
  add_test(
    NAME test_progressive_load
    COMMAND $<TARGET_FILE:test_progressive_load-bin>)
  add_test(
    NAME test_queue_input
    COMMAND $<TARGET_FILE:test_queue_input-bin>)
  add_test(
    NAME test_batch_vs_streaming
    COMMAND $<TARGET_FILE:test_batch_vs_streaming-bin>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_traces/scale/huge_2000jobs.csv)
  add_test(
    NAME test_block_queue
    COMMAND $<TARGET_FILE:test_block_queue-bin>)
  add_test(
    NAME test_capacity_schedule
    COMMAND $<TARGET_FILE:test_capacity_schedule-bin>)
  add_test(
    NAME test_warm_start
    COMMAND $<TARGET_FILE:test_warm_start-bin>)
  add_test(
    NAME test_warm_start_validation
    COMMAND /bin/bash
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_warm_start_validation_tests.sh
      $<TARGET_FILE:sim-bin>)
  add_test(
    NAME test_max_time
    COMMAND /bin/bash
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_max_time_tests.sh
      $<TARGET_FILE:sim-bin> $<TARGET_FILE:trace-bin>)
  add_test(
    NAME test_cli_input
    COMMAND /bin/bash
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_input_tests.sh
      $<TARGET_FILE:sim-bin>)
  add_test(
    NAME test_swf
    COMMAND /bin/bash
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_swf_tests.sh
      $<TARGET_FILE:sim-bin>)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME test_trace_tools
      COMMAND ${CMAKE_COMMAND} -E env
        DR_EVT_SIMULATOR=$<TARGET_FILE:sim-bin>
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_trace_tools.py)
    set_tests_properties(test_trace_tools PROPERTIES
      LABELS "unit;scripts"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(
      NAME test_python_performance_dispatch
      COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/experimental/multi-cluster/tests/test_performance_dispatch.py)
    set_tests_properties(test_python_performance_dispatch PROPERTIES
      LABELS "unit;scripts;multi-cluster"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  set_tests_properties(
    test_append_job_api test_custom_scheduler test_progressive_load
    test_queue_input
    test_batch_vs_streaming test_block_queue test_capacity_schedule
    test_warm_start test_warm_start_validation test_max_time test_cli_input
    test_swf
    PROPERTIES
      LABELS "unit;native"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RUN_SERIAL TRUE)

  if(MPI_CXX_FOUND AND MPIEXEC_EXECUTABLE)
    add_test(
      NAME test_mpi_streaming
      COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
        ${MPIEXEC_PREFLAGS} $<TARGET_FILE:test_mpi_streaming-bin>
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_traces/scale/xlarge_500jobs.csv
        --total_nodes 795 ${MPIEXEC_POSTFLAGS})
    set_tests_properties(test_mpi_streaming PROPERTIES
      LABELS "unit;native;mpi"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RUN_SERIAL TRUE)
    if(DR_EVT_HAS_SER20 AND Python3_Interpreter_FOUND)
      add_test(
        NAME test_mpi_performance_dispatch
        COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mpi_performance_dispatch.py
          ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG}
          $<TARGET_FILE:mpi_performance_dispatch-bin>
          ${CMAKE_CURRENT_SOURCE_DIR})
      set_tests_properties(test_mpi_performance_dispatch PROPERTIES
        LABELS "unit;native;mpi"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 120)
    endif()
  endif()
endif()

# The test sources use assert() for executable checks. Release configurations
# define NDEBUG by default, which would otherwise compile those checks (and any
# expressions inside them) out of the test binaries.
foreach(DR_EVT_TEST_TARGET IN LISTS DR_EVT_UNIT_TEST_TARGETS)
  if(MSVC)
    target_compile_options(${DR_EVT_TEST_TARGET} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${DR_EVT_TEST_TARGET} PRIVATE -UNDEBUG)
  endif()
endforeach()

# Install the complete Boost development payload only when this configure
# built Boost via FetchContent. An old _deps directory must not cause
# installation when a system Boost was selected instead.
if(DR_EVT_BOOST_FETCHCONTENT)
  # Install every shared Boost library that the fetched build produced, not
  # merely the components linked directly by DR_EVT. Boost components can
  # carry runtime dependencies of their own (for example,
  # program_options -> boost_atomic), which must accompany installed tools.
  # These files may not exist until after the first build, so the glob must
  # run from the generated install script—not while CMake configures.
  install(CODE "
    file(GLOB_RECURSE DR_EVT_BOOST_SHARED_LIBS LIST_DIRECTORIES false
      \"${CMAKE_BINARY_DIR}/_deps/boost-build/libs/libboost*.so*\")
    if(DR_EVT_BOOST_SHARED_LIBS)
      file(INSTALL DESTINATION \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
           TYPE FILE FILES \${DR_EVT_BOOST_SHARED_LIBS})
    endif()
  ")

  # Boost's own subdirectory is EXCLUDE_FROM_ALL, so install its development
  # headers through one guarded script. The version check avoids traversing
  # thousands of files again when the effective destination already contains
  # this fetched Boost version (including with `cmake --install --prefix`).
  file(GLOB DR_EVT_BOOST_HEADER_TREES LIST_DIRECTORIES true
       "${boost_SOURCE_DIR}/libs/*/include/boost")
  file(STRINGS "${boost_SOURCE_DIR}/libs/config/include/boost/version.hpp"
       DR_EVT_FETCHED_BOOST_VERSION_LINE
       REGEX "^#define BOOST_VERSION [0-9]+$"
       LIMIT_COUNT 1)
  string(REGEX MATCH "[0-9]+$" DR_EVT_FETCHED_BOOST_VERSION
         "${DR_EVT_FETCHED_BOOST_VERSION_LINE}")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/InstallFetchedBoost.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InstallFetchedBoost.cmake"
    @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallFetchedBoost.cmake"
          COMPONENT Development)
  unset(DR_EVT_BOOST_HEADER_TREES)
  unset(DR_EVT_FETCHED_BOOST_VERSION_LINE)
  unset(DR_EVT_FETCHED_BOOST_VERSION)
endif()

# Install the binaries
install(
  TARGETS  ${DR_EVT_EXEC_TARGETS}
  EXPORT   DR_EVT_Targets
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

install(
  TARGETS  ${DR_EVT_UNIT_TEST_TARGETS}
#  EXPORT   DR_EVT_Targets
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}/tests COMPONENT Tests
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Tests
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Tests
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

# Install
# It is used in the utils/generate_cxx_code.cpp file
set (DR_EVT_SRC_DIR ${CMAKE_SOURCE_DIR})

# Write the configure file
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/configure_files/dr_evt_config.hpp.in"
  "${CMAKE_BINARY_DIR}/dr_evt_config.hpp"
  @ONLY)
target_compile_definitions(dr_evt PUBLIC DR_EVT_HAS_CONFIG)

if (TARGET DR_EVT_CXX_FLAGS_werror)
  target_link_libraries(dr_evt PUBLIC DR_EVT_CXX_FLAGS_werror)
endif ()

target_link_libraries(dr_evt PUBLIC ${DL_LIBRARY})

target_link_libraries(dr_evt PUBLIC ${Boost_LIBRARIES})

# Clean things up
include(DR_EVT_DebugUtilities)
dr_evt_remove_default_include_paths_from_all_subtargets(dr_evt)
# This is to fix a bug with certain compilers interacting poorly with
# NVCC. In particular, the NVCC include line would have things like
# "-isystem=/usr/include" which would cause issues resolving STL
# include paths. Since compilers include "/usr/include" in their
# system include paths anyway (things searched by #include <...>), we
# can safely remove these from the explicit link line.

if (DR_EVT_DEBUG_PRINT_SUBTARGETS)
  dr_evt_print_all_subtargets(dr_evt)
endif ()

# Add the rest of the things
#add_subdirectory(tests)
#add_subdirectory(scripts)

################################################################
# Install DR_EVT
################################################################

include(CMakePackageConfigHelpers)

# Write the version file. This is independent of build/install tree.
write_basic_package_version_file(
  DR_EVT_ConfigVersion.cmake
  VERSION "${DR_EVT_VERSION}"
  COMPATIBILITY SameMajorVersion)

# This is for the build tree
set(INCLUDE_INSTALL_DIRS
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/params"
  "${CMAKE_SOURCE_DIR}/src/sim"
  "${CMAKE_SOURCE_DIR}/src/trace"
  "${CMAKE_SOURCE_DIR}/src/utils")
if (DR_EVT_HAS_PROTOBUF)
  list(APPEND INCLUDE_INSTALL_DIRS "${CMAKE_SOURCE_DIR}/src/proto")
endif()
set(LIB_INSTALL_DIR "${CMAKE_BINARY_DIR}")
set(EXTRA_CMAKE_MODULE_DIR "${CMAKE_SOURCE_DIR}/cmake/modules")
configure_package_config_file(cmake/configure_files/DR_EVT_Config.cmake.in
  "${CMAKE_BINARY_DIR}/DR_EVT_Config.cmake"
  INSTALL_DESTINATION "${CMAKE_BINARY_DIR}"
  PATH_VARS INCLUDE_INSTALL_DIRS LIB_INSTALL_DIR)

# Write the configure file for the install tree
set(INCLUDE_INSTALL_DIRS include)
set(LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR})
set(CMAKE_INSTALL_DIR ${LIB_INSTALL_DIR}/cmake/dr_evt)
set(EXTRA_CMAKE_MODULE_DIR)
configure_package_config_file(cmake/configure_files/DR_EVT_Config.cmake.in
  "${CMAKE_BINARY_DIR}/DR_EVT_Config.cmake.install"
  INSTALL_DESTINATION "${CMAKE_INSTALL_DIR}"
  PATH_VARS INCLUDE_INSTALL_DIRS LIB_INSTALL_DIR)


# Install library
install(
  TARGETS dr_evt
  EXPORT DR_EVT_Targets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )

# Build tree export
#export(EXPORT DR_EVT_Targets NAMESPACE DR_EVT:: FILE DR_EVT_Targets.cmake)

# Install export
# Temporarily disabled due to Boost FetchContent export issues
#install(EXPORT DR_EVT_Targets
#  NAMESPACE DR_EVT::
#  FILE DR_EVT_Targets.cmake
#  DESTINATION "${CMAKE_INSTALL_DIR}")

# Install the cmake stuff
install(FILES
  "${PROJECT_BINARY_DIR}/DR_EVT_Config.cmake.install"
  DESTINATION "${CMAKE_INSTALL_DIR}"
  RENAME "DR_EVT_Config.cmake")
install(FILES "${PROJECT_BINARY_DIR}/DR_EVT_ConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_DIR})
install(DIRECTORY cmake/modules
  DESTINATION "${CMAKE_INSTALL_DIR}"
  FILES_MATCHING PATTERN "*.cmake")

# Install header files
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/params"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
if (DR_EVT_HAS_PROTOBUF)
  install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/src/proto"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
endif()
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/trace"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/sim"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/utils"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(
  FILES "${PROJECT_BINARY_DIR}/dr_evt_config.hpp"
        "${PROJECT_SOURCE_DIR}/src/dr_evt_types.hpp"
        "${PROJECT_SOURCE_DIR}/src/boost.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(PROGRAMS "${PROJECT_SOURCE_DIR}/scripts/stitch_checkpoint_output.py"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
  RENAME dr_evt_stitch_checkpoint_output)

if (DR_EVT_HAS_EXPRTK)
  install(
    FILES ${EXPRTK_DIR}/${EXPRTK_HEADER}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif ()

# Install contributor list, license, readme
install(
  FILES #"${PROJECT_SOURCE_DIR}/CONTRIBUTORS"
        #"${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/README.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")

################################################################
# Configuration summary
################################################################

# This creates a formatted string that contains a list of variables,
# one per line, with their values interpreted as TRUE or FALSE. The
# purpose is to provide uniform output, rather than an odd mixture of
# "1", "0", "ON", "OFF", "TRUE" and "FALSE".
macro(append_str_tf STRING_VAR)
  dr_evt_get_max_str_length(_max_length ${ARGN})
  math(EXPR _max_length "${_max_length} + 2")

  foreach(var ${ARGN})
    string(LENGTH "${var}" _var_length)
    math(EXPR _num_spaces "${_max_length} - ${_var_length}")
    dr_evt_get_space_string(_spaces ${_num_spaces})
    if (${var})
      set(${var} "TRUE")
      string(APPEND ${STRING_VAR} "  ${var}:" "${_spaces}" "TRUE\n")
    else ()
      set(${var} "FALSE")
      string(APPEND ${STRING_VAR} "  ${var}:" "${_spaces}" "FALSE\n")
    endif ()
  endforeach()
endmacro ()

# NOTE: message() outputs to stderr by default. We now use a string to
# maintain this information and then have cmake echo it to stdout. The
# only side effects are that if you use the CMake GUI, you won't see
# this output anymore (they only report stderr) and that if you add
# something to the list, you must remember your newline!
set(_str "\n== DR_EVT Configuration Summary ==\n\n")
string(APPEND _str "  PROJECT_SOURCE_DIR:   ${PROJECT_SOURCE_DIR}\n"
  "  PROJECT_BINARY_DIR:   ${PROJECT_BINARY_DIR}\n\n"
  "  CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}\n"
  "  CMAKE_BUILD_TYPE:     ${CMAKE_BUILD_TYPE}\n\n")
if (CMAKE_BUILD_TYPE MATCHES None)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS}\n")
elseif (CMAKE_BUILD_TYPE MATCHES Release)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}\n")
elseif (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}\n")
elseif (CMAKE_BUILD_TYPE MATCHES Debug)
  string(APPEND _str
    "  CXX FLAGS:            ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}\n")
endif ()
string(APPEND _str "\n")

#Print the true/false guys
append_str_tf(_str
  DR_EVT_GNU_LINUX
  DR_EVT_HAS_SER20
  DR_EVT_HAS_REDIS_PLUS_PLUS
  DR_EVT_HAS_CATCH2
  DR_EVT_HAS_DOXYGEN
  DR_EVT_HAS_OPENMP
  DR_EVT_HAS_NUMA
  DR_EVT_HAS_PROTOBUF
  DR_EVT_INSTALL_FETCHED_GRPC
  DR_EVT_GPROF)
string(APPEND _str
  "\n== End DR_EVT Configuration Summary ==\n")

# Output to stdout
execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${_str}")
set(_str)

#
# Write a basic modulefile
#
set(DR_EVT_MODULEFILE_NAME "dr_evt-${DR_EVT_VERSION}.lua"
  CACHE STRING
  "The name of the DR_EVT modulefile to install. Must end in .lua.")

if (NOT (DR_EVT_MODULEFILE_NAME MATCHES ".+\.lua"))
  message(WARNING
    "DR_EVT_MODULEFILE_NAME must have extension \".lua\". Appending.")
  set(DR_EVT_MODULEFILE_NAME "${DR_EVT_MODULEFILE_NAME}.lua"
    CACHE STRING "" FORCE)
endif ()

configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/configure_files/dr_evt_module.lua.in"
  "${CMAKE_BINARY_DIR}/dr_evt_module.lua.install"
  @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/dr_evt_module.lua.install"
  RENAME "${DR_EVT_MODULEFILE_NAME}"
  DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/modulefiles")

################################################################
# Python bindings (optional)
################################################################

if(DR_EVT_HAS_PYBIND11)
  message(STATUS "Building Python bindings...")

  # Let CMake select the compiler's supported IPO flags before pybind11
  # creates the module.  In particular, modern CMake uses GCC's
  # parallel-capable -flto=auto instead of pybind11's plain -flto.
  set(DR_EVT_PYTHON_IPO_WAS_UNDEFINED FALSE)
  if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT DR_EVT_PYTHON_IPO_SUPPORTED
                        OUTPUT DR_EVT_PYTHON_IPO_ERROR
                        LANGUAGES CXX)
    if(DR_EVT_PYTHON_IPO_SUPPORTED)
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
      set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
      message(WARNING
        "IPO is unavailable for the Python module: "
        "${DR_EVT_PYTHON_IPO_ERROR}")
    endif()
    set(DR_EVT_PYTHON_IPO_WAS_UNDEFINED TRUE)
  endif()

  # pybind11 enables LTO as one of its default "extras" in Release builds.
  # Suppress those extras for gprof so every target follows the profiling-safe
  # no-LTO policy established by SetupCXX.cmake.
  set(DR_EVT_PYBIND11_OPTIONS)
  if(DR_EVT_GPROF)
    list(APPEND DR_EVT_PYBIND11_OPTIONS NO_EXTRAS)
  endif()
  pybind11_add_module(dr_evt_python ${DR_EVT_PYBIND11_OPTIONS}
    python/dr_evt_bindings.cpp
  )
  unset(DR_EVT_PYBIND11_OPTIONS)
  if(DR_EVT_PYTHON_IPO_WAS_UNDEFINED)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  endif()
  unset(DR_EVT_PYTHON_IPO_WAS_UNDEFINED)
  unset(DR_EVT_PYTHON_IPO_SUPPORTED)
  unset(DR_EVT_PYTHON_IPO_ERROR)

  target_link_libraries(dr_evt_python PRIVATE dr_evt)

  target_include_directories(dr_evt_python PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARY_DIR}/src
  )

  # Set output name
  set_target_properties(dr_evt_python PROPERTIES
    OUTPUT_NAME dr_evt
    PREFIX "${PYTHON_MODULE_PREFIX}"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
  )

  # Keep the Python module in this installation's selected library directory
  # instead of a system site-packages directory, which may be read-only.
  set(DR_EVT_PYTHON_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/python")

  install(TARGETS dr_evt_python
    LIBRARY DESTINATION ${DR_EVT_PYTHON_INSTALL_DIR} COMPONENT Python
  )

  message(STATUS "Python module will be installed to: ${CMAKE_INSTALL_PREFIX}/${DR_EVT_PYTHON_INSTALL_DIR}")
  message(STATUS "Add to PYTHONPATH: export PYTHONPATH=${CMAKE_INSTALL_PREFIX}/${DR_EVT_PYTHON_INSTALL_DIR}:$PYTHONPATH")
endif()
