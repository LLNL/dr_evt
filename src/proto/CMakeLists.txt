# Create the dr_evt_proto library
if (DR_EVT_HAS_PROTOBUF)
    enable_language(C)

    set_full_path(PROTO_INPUTS
        dr_evt_params.proto
    )

    foreach (proto IN LISTS PROTO_INPUTS)
        get_filename_component(name "${proto}" NAME_WE)
        list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc")
        list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h")
        #list(APPEND PROTO_PY "${CMAKE_CURRENT_BINARY_DIR}/proto/${name}_pb2.py")
    endforeach ()

    add_custom_command(
        COMMAND protobuf::protoc
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        --python_out=${CMAKE_CURRENT_BINARY_DIR}
        --proto_path=${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/protobuf
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
        ${PROTO_INPUTS}
        OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_PY}
        DEPENDS ${PROTO_INPUTS} protobuf::protoc
        COMMENT "Running protoc on DR_EVT protobuf sources."
        COMMAND_EXPAND_LISTS
        VERBATIM)

    add_custom_target(dr_evt_proto_genSrc
        DEPENDS ${PROTO_SRCS} ${PROTO_HDRS})

    add_library(dr_evt_proto ${PROTO_SRCS} ${PROTO_HDRS})
    target_link_libraries(dr_evt_proto PUBLIC protobuf::libprotobuf)
    target_include_directories(dr_evt_proto SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
    target_include_directories(dr_evt_proto PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    add_dependencies(dr_evt_proto dr_evt_proto_genSrc)

    # Install the library
    install(TARGETS dr_evt_proto
        EXPORT DR_EVT_Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/proto
    )

    # Install the newly built headers
    install(FILES ${PROTO_HDRS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/proto)

    # Install the Python module into the site-packages directory
    if (PROTO_PY)
        install(FILES ${PROTO_PY} DESTINATION
                ${PYTHON_INSTALL_PREFIX}/${CMAKE_INSTALL_PYTHONDIR})
    endif (PROTO_PY)


    # Add the header files for this directory
    set_full_path(THIS_DIR_HEADERS
        utils.hpp
        dr_evt_params.hpp
    )

    # Add the source files for this directory
    set_full_path(THIS_DIR_SOURCES
        utils.cpp
        dr_evt_params.cpp
    )

    # Add the subdirectories
    #add_subdirectory(subdir)
endif (DR_EVT_HAS_PROTOBUF)


# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
