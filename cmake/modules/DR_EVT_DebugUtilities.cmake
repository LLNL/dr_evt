include(CMakePrintHelpers)

# Assume that the potential "subtargets" are LINK_LIBRARIES of some
# sort or another.
function (dr_evt_get_all_subtargets TARGET OUTPUT_VAR)
  get_target_property(_target_type ${TARGET} TYPE)

  set(_target_link_libraries)
  set(_target_interface_link_libraries)
  set(_target_all_subtargets)
  if (NOT ("${_target_type}" STREQUAL "INTERFACE_LIBRARY"))
    get_target_property(_target_link_libraries ${TARGET} LINK_LIBRARIES)
  endif ()
  get_target_property(_target_interface_link_libraries ${TARGET}
    INTERFACE_LINK_LIBRARIES)

  foreach (_lib IN LISTS _target_link_libraries _target_interface_link_libraries)
    if (TARGET "${_lib}")
      list(APPEND _target_all_subtargets "${_lib}")
    endif ()
  endforeach ()
  if (_target_all_subtargets)
    list(REMOVE_DUPLICATES _target_all_subtargets)
  endif ()

  set(${OUTPUT_VAR} "${_target_all_subtargets}" PARENT_SCOPE)
endfunction ()

# This function recursively descends into targets and prints all of
# the information about each target
function (dr_evt_print_all_subtargets TARGET)

  # TODO: Allow users to specify which properties they want printed
  # via COMMON_PROPERTIES, INTERFACE_ONLY_PROPERTIES, and
  # NONINTERFACE_ONLY_PROPERTIES

  # This returns a list of targets being linked in.
  dr_evt_get_all_subtargets(${TARGET} _target_all_subtargets)

  if (_target_all_subtargets)
    foreach (tgt IN LISTS _target_all_subtargets)
      dr_evt_print_all_subtargets(${tgt})
    endforeach ()
  endif ()

  # Handle this target
  get_target_property(_target_type ${TARGET} TYPE)
  if ("${_target_type}" STREQUAL "INTERFACE_LIBRARY")
    cmake_print_properties(TARGETS ${TARGET}
      PROPERTIES
      INTERFACE_LINK_LIBRARIES
      INTERFACE_INCLUDE_DIRECTORIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
      INTERFACE_COMPILE_DEFINITIONS
      INTERFACE_COMPILE_FEATURES
      INTERFACE_COMPILE_OPTIONS)
  else ()
    cmake_print_properties(TARGETS ${TARGET}
      PROPERTIES
      LINK_LIBRARIES
      LINK_FLAGS
      INCLUDE_DIRECTORIES
      COMPILE_DEFINITIONS
      COMPILE_FLAGS
      COMPILE_FEATURES
      COMPILE_OPTIONS
      INTERFACE_LINK_LIBRARIES
      INTERFACE_INCLUDE_DIRECTORIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
      INTERFACE_COMPILE_DEFINITIONS
      INTERFACE_COMPILE_FEATURES
      INTERFACE_COMPILE_OPTIONS)
  endif ()
endfunction ()

# This function removes "blacklisted" include paths from the given
# list. Blacklisted paths are those that compilers include anyway
# (things of the "/usr/include" variety).
function (dr_evt_remove_default_include_paths_from_list OUTPUT_VAR INCL_LIST)
  set(_blacklisted_incl_dirs
    "/usr/include"
    "/usr/local/include")

  set(_tmp_list)
  foreach (dir IN LISTS INCL_LIST)
    list(FIND _blacklisted_incl_dirs "${dir}" _idx)
    if (_idx EQUAL -1)
      list(APPEND _tmp_list "${dir}")
    endif ()
  endforeach ()

  # Return the new list
  set(${OUTPUT_VAR} ${_tmp_list} PARENT_SCOPE)
endfunction ()

function (dr_evt_remove_default_include_paths_from_target TARGET)
  get_target_property(_target_type ${TARGET} TYPE)

  set(_interface_prop_names
    INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)

  # If it's a "real" target, INCLUDE_DIRECTORIES is a valid property
  if (NOT ("${_target_type}" STREQUAL "INTERFACE_LIBRARY"))
    list(APPEND _interface_prop_names INCLUDE_DIRECTORIES)
  endif ()

  foreach (prop IN LISTS _interface_prop_names)
    get_target_property(_tgt_incl_dirs ${TARGET} ${prop})
    if (_tgt_incl_dirs)
      dr_evt_remove_default_include_paths_from_list(
        _clean_incl_dirs "${_tgt_incl_dirs}")
      set_target_properties(${TARGET}
        PROPERTIES ${prop} "${_clean_incl_dirs}")
    endif ()
  endforeach ()
endfunction ()

function (dr_evt_remove_default_include_paths_from_all_subtargets TARGET)
  # This returns a list of targets being linked in.
  dr_evt_get_all_subtargets(${TARGET} _target_all_subtargets)

  # Recur first
  foreach (tgt IN LISTS _target_all_subtargets)
    dr_evt_remove_default_include_paths_from_all_subtargets("${tgt}")
  endforeach ()

  dr_evt_remove_default_include_paths_from_target(${TARGET})
endfunction ()
