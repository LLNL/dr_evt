cmake_minimum_required(VERSION 3.12)

project(DR_EVT CXX)

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
set(DR_EVT_VERSION_MINOR 1)
set(DR_EVT_VERSION_PATCH 0)

# Build with at least C++17 standard; allow newer standards.
if (NOT CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD EQUAL 98
    OR CMAKE_CXX_STANDARD EQUAL 11)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
  set(CMAKE_CXX_EXTENSIONS ON)
endif ()

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

# Cereal is going to be required.
option(DR_EVT_WITH_CEREAL "Include Cereal" ON)

option(DR_EVT_WITH_OPENMP "Enable OpenMP" OFF)

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
#  well especially with Catch2 due to the incomplete support for C++17.
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

# Get installation directories -- these get used in various places;
# best to just make them available
include(GNUInstallDirs)
include(ExternalProject)
include(SetupCXX)
include(SetupProtobuf)

################################################################
# Initialize dependencies
################################################################

# Required dependencies

if (NOT DR_EVT_HAS_PROTOBUF)
  if (BUILD_PROTOBUF)
    set(To_Ignore ${BOOST_ROOT})
    message(STATUS "!!!!!!!!!!!!!!!!!!!!!!! Attention !!!!!!!!!!!!!!!!!!!!!!!!!")
    message(STATUS "Type `make` to build protobuf, which is a required package.")
    message(STATUS "Then, rerun cmake in the exactly the same way, that is to")
    message(STATUS "configure DR_EVT with the protobuf just installed.")
    message(STATUS "Alternatively, use -DPROTOBUF_ROOT:PATH=<path-to-protobuf>")
    message(STATUS "to avoid two-staged building.")
    message(STATUS "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
    return ()
  else (BUILD_PROTOBUF)
    message(FATAL_ERROR "protobuf is required!")
  endif (BUILD_PROTOBUF)
endif (NOT DR_EVT_HAS_PROTOBUF)

if (DEFINED BOOST_ROOT)
    message(STATUS "BOOST_ROOT: " ${BOOST_ROOT})
    set(Boost_NO_SYSTEM_PATHS ON)
else ()
    if (DEFINED ENV{BOOST_ROOT})
        message(STATUS "ENV BOOST_ROOT: " $ENV{BOOST_ROOT})
        set(Boost_NO_SYSTEM_PATHS ON)
    endif ()
endif ()

# boost::multi_index is needed but it is header-only
find_package(Boost
    #         HINTS ${BOOST_ROOT} $ENV{BOOST_ROOT}
             REQUIRED COMPONENTS
             regex filesystem system program_options)

message(STATUS "Boost_INCLUDE_DIRS: " ${Boost_INCLUDE_DIRS})
message(STATUS "Boost_LIBRARY_DIRS: " ${Boost_LIBRARY_DIRS})

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

if (DR_EVT_HAS_STD_FILESYSTEM)
  set(LIB_FILESYSTEM "stdc++fs")
else ()
  set(LIB_FILESYSTEM ${Boost_LIBRARIES})
endif ()

# Optional dependencies

if (DR_EVT_WITH_CEREAL)
  include(SetupCereal)
endif (DR_EVT_WITH_CEREAL)


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

# Create the DR_EVT library
add_library(dr_evt ${DR_EVT_SOURCES} ${DR_EVT_HEADERS})
if (DR_EVT_HAS_PROTOBUF)
  add_dependencies(dr_evt protobuf::libprotobuf)
  add_dependencies(dr_evt protobuf::protoc)
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
target_include_directories(dr_evt SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})

# Use the IMPORTED targets when possible.


if (DR_EVT_HAS_CEREAL)
  add_dependencies(dr_evt CEREAL-download)
  target_include_directories(dr_evt PUBLIC
    $<BUILD_INTERFACE:${CEREAL_HEADER_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
endif (DR_EVT_HAS_CEREAL)

if (DR_EVT_WITH_UNIT_TESTING)
  add_dependencies(dr_evt CATCH2)
  target_include_directories(dr_evt PUBLIC
    $<BUILD_INTERFACE:${CATCH2_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/catch2>)
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

target_link_libraries(sim-bin PRIVATE dr_evt ${LIB_FILESYSTEM})
set_target_properties(sim-bin PROPERTIES OUTPUT_NAME simulator ${Boost_LIBRARIES})
set_target_properties(sim-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_EXEC_TARGETS sim-bin)

# add executable tracer
add_executable( trace-bin src/trace.cpp )
target_include_directories(trace-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(trace-bin PRIVATE dr_evt ${LIB_FILESYSTEM} ${Boost_LIBRARIES})
set_target_properties(trace-bin PROPERTIES OUTPUT_NAME tracer)
set_target_properties(trace-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
list(APPEND DR_EVT_EXEC_TARGETS trace-bin)


# Build tests
add_executable( t_state_rngen-bin src/utils/unit_tests/t_state_rngen.cpp )
target_include_directories(t_state_rngen-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_state_rngen-bin PRIVATE dr_evt ${LIB_FILESYSTEM})
set_target_properties(t_state_rngen-bin PROPERTIES OUTPUT_NAME t_state_rngen)
set_target_properties(t_state_rngen-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state_rngen-bin)

add_executable( t_state_cereal-bin src/utils/unit_tests/t_state_cereal.cpp )
target_include_directories(t_state_cereal-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_state_cereal-bin PRIVATE dr_evt ${LIB_FILESYSTEM})
set_target_properties(t_state_cereal-bin PROPERTIES OUTPUT_NAME t_state_cereal)
set_target_properties(t_state_cereal-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state_cereal-bin)


add_executable( t_state-bin src/utils/unit_tests/t_state.cpp )

target_include_directories(t_state-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_state-bin PRIVATE dr_evt ${LIB_FILESYSTEM})
set_target_properties(t_state-bin PROPERTIES OUTPUT_NAME t_state)
set_target_properties(t_state-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_state-bin)

add_executable( t_rngen-bin src/utils/unit_tests/t_rngen.cpp )

target_include_directories(t_rngen-bin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(t_rngen-bin PRIVATE dr_evt ${LIB_FILESYSTEM})
set_target_properties(t_rngen-bin PROPERTIES OUTPUT_NAME t_rngen)
set_target_properties(t_rngen-bin PROPERTIES CMAKE_INSTALL_RPATH
                      "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

list(APPEND DR_EVT_UNIT_TEST_TARGETS t_rngen-bin)

# Install the binaries
install(
  TARGETS  ${DR_EVT_EXEC_TARGETS}
  EXPORT   DR_EVT_Targets
  RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

install(
  TARGETS  ${DR_EVT_UNIT_TEST_TARGETS}
#  EXPORT   DR_EVT_Targets
  RUNTIME  DESTINATION ${CMAKE_INSTALL_PREFIX}/tests/unit
  ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
  "${CMAKE_SOURCE_DIR}/src/proto"
  "${CMAKE_SOURCE_DIR}/src/sim"
  "${CMAKE_SOURCE_DIR}/src/trace"
  "${CMAKE_SOURCE_DIR}/src/utils")
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
install(EXPORT DR_EVT_Targets
  NAMESPACE DR_EVT::
  FILE DR_EVT_Targets.cmake
  DESTINATION "${CMAKE_INSTALL_DIR}")

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
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/proto"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
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

if (DR_EVT_HAS_EXPRTK)
  install(
    FILES ${EXPRTK_DIR}/${EXPRTK_HEADER}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif ()

# TODO: either need to install the whole directory or not at all
if (DR_EVT_HAS_CEREAL)
  install(
    DIRECTORY "${CEREAL_HEADER_DIR}/cereal"
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
  DR_EVT_HAS_CEREAL
  DR_EVT_HAS_CATCH2
  DR_EVT_HAS_DOXYGEN
  DR_EVT_HAS_OPENMP
  DR_EVT_HAS_NUMA
  DR_EVT_HAS_PROTOBUF)
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
