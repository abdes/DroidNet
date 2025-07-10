# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Determine if this is built as a subproject  or if it is the master project.
# By default we only add the install target if this is the master project. If
# it is used as a sub-project, then the user can request to add the install
# targets by overriding this option.
if(NOT DEFINED ${META_PROJECT_ID}_INSTALL)
  option(
    ${META_PROJECT_ID}_INSTALL
    "Generate the install target for this project."
    ${${META_PROJECT_ID}_IS_MASTER_PROJECT}
  )
endif()

macro(_setup_install_dirs)
  message(STATUS "Using CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
  # Check for system dir install
  set(_system_dir_install FALSE)
  if(
    "${CMAKE_INSTALL_PREFIX}"
      STREQUAL
      "/usr"
    OR
      "${CMAKE_INSTALL_PREFIX}"
        STREQUAL
        "/usr/local"
  )
    set(_system_dir_install TRUE)
  endif()

  # cmake-format: off
  if(UNIX AND _system_dir_install)
    # Installation paths
    include(GNUInstallDirs)
    # Install into the system (/usr/bin or /usr/local/bin)
    set(OXYGEN_INSTALL_LIB "${CMAKE_INSTALL_LIBDIR}") # /usr/[local]/lib
    set(OXYGEN_INSTALL_SHARED "${OXYGEN_INSTALL_LIB}") # /usr/[local]/lib
    set(
      OXYGEN_INSTALL_CMAKE
      "${CMAKE_INSTALL_DATAROOTDIR}/cmake/${META_PROJECT_NAME}"
    ) # /usr/[local]/share/cmake/<project>
    set(OXYGEN_INSTALL_PKGCONFIG "${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig") # /usr/[local]/share/pkgconfig
    set(
      OXYGEN_INSTALL_EXAMPLES
      "${CMAKE_INSTALL_DATAROOTDIR}/${META_PROJECT_NAME}/examples"
    ) # /usr/[local]/share/<project>/examples
    set(OXYGEN_INSTALL_DATA "${CMAKE_INSTALL_DATAROOTDIR}/${META_PROJECT_NAME}") # /usr/[local]/share/<project>
    set(OXYGEN_INSTALL_BIN "${CMAKE_INSTALL_BINDIR}") # /usr/[local]/bin
    set(OXYGEN_INSTALL_INCLUDE "${CMAKE_INSTALL_INCLUDEDIR}") # /usr/[local]/include
    set(OXYGEN_INSTALL_DOC "${CMAKE_INSTALL_DOCDIR}") # /usr/[local]/share/doc/<project>
    set(OXYGEN_INSTALL_SHORTCUTS "${CMAKE_INSTALL_DATAROOTDIR}/applications") # /usr/[local]/share/applications
    set(OXYGEN_INSTALL_ICONS "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps") # /usr/[local]/share/pixmaps
    set(OXYGEN_INSTALL_INIT "/etc/init") # /etc/init (upstart init scripts)
    set(OXYGEN_INSTALL_MISC "${CMAKE_INSTALL_DATAROOTDIR}/${META_PROJECT_NAME}") # /etc/init (upstart init scripts)
  else()
    # Install into local directory
    set(OXYGEN_INSTALL_LIB "${CMAKE_INSTALL_PREFIX}/lib") # ./lib
    set(OXYGEN_INSTALL_BIN "${CMAKE_INSTALL_PREFIX}/bin") # ./bin
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
      set(OXYGEN_INSTALL_SHARED "${OXYGEN_INSTALL_BIN}") # ./lib
    else()
      set(OXYGEN_INSTALL_SHARED "${OXYGEN_INSTALL_LIB}") # ./lib
    endif()
    set(
      OXYGEN_INSTALL_CMAKE
      "${CMAKE_INSTALL_PREFIX}/share/cmake/${META_PROJECT_NAME}"
    ) # ./share/cmake/<project>
    set(OXYGEN_INSTALL_PKGCONFIG "${CMAKE_INSTALL_PREFIX}/share/pkgconfig") # ./share/pkgconfig
    set(OXYGEN_INSTALL_EXAMPLES "${CMAKE_INSTALL_PREFIX}/examples") # ./examples
    set(OXYGEN_INSTALL_DATA "${CMAKE_INSTALL_PREFIX}") # ./
    set(OXYGEN_INSTALL_INCLUDE "${CMAKE_INSTALL_PREFIX}/include") # ./include
    set(OXYGEN_INSTALL_DOC "${CMAKE_INSTALL_PREFIX}/doc") # ./doc
    set(OXYGEN_INSTALL_SHORTCUTS "${CMAKE_INSTALL_PREFIX}/shortcuts") # ./shortcuts
    set(OXYGEN_INSTALL_ICONS "${CMAKE_INSTALL_PREFIX}/icons") # ./icons
    set(OXYGEN_INSTALL_INIT "${CMAKE_INSTALL_PREFIX}/init") # ./init
    set(OXYGEN_INSTALL_MISC "${CMAKE_INSTALL_PREFIX}") # ./
  endif()
  # cmake-format: on
endmacro()

if(${META_PROJECT_ID}_INSTALL)
  _setup_install_dirs()

  if(NOT ${META_PROJECT_ID}_IS_MASTER_PROJECT)
    return()
  endif()

  set(runtime "${META_PROJECT_NAME}_runtime")
  set(dev "${META_PROJECT_NAME}_dev")
  set(meta "${META_PROJECT_NAME}_meta")
  set(data "${META_PROJECT_NAME}_data")
  set(docs "${META_PROJECT_NAME}_docs")

  # Install the project meta files
  install(FILES AUTHORS DESTINATION ${OXYGEN_INSTALL_MISC} COMPONENT ${meta})
  install(FILES LICENSE DESTINATION ${OXYGEN_INSTALL_MISC} COMPONENT ${meta})
  install(FILES README.md DESTINATION ${OXYGEN_INSTALL_MISC} COMPONENT ${meta})

  # # Install master docs
  # string(MAKE_C_IDENTIFIER ${META_PROJECT_NAME} project_id)
  # string(TOLOWER ${project_id} project_id)
  # set(master_sphinx_target ${project_id}_master)
  # install(DIRECTORY ${SPHINX_BUILD_DIR}/${master_sphinx_target} DESTINATION ${OXYGEN_INSTALL_DOC} COMPONENT ${docs} OPTIONAL)

  # # Install data
  # install(DIRECTORY ${PROJECT_SOURCE_DIR}/data DESTINATION ${OXYGEN_INSTALL_DATA} COMPONENT ${data} OPTIONAL)
endif()

# Must install the export separately for all modules that use the same export name.
#
# Example:
# --------
# install(
#   EXPORT asap
#   NAMESPACE asap::
#   DESTINATION ${OXYGEN_INSTALL_CMAKE}
#   FILE "asap-targets.cmake"
#   COMPONENT Development)

function(oxygen_module_install)
  if(NOT ${META_PROJECT_ID}_INSTALL)
    return()
  endif()

  set(options)
  set(
    oneValueArgs
    EXPORT
    INCLUDE_PREFIX
  )
  set(multiValueArgs TARGETS)

  cmake_parse_arguments(
    x
    "${options}"
    "${oneValueArgs}"
    "${multiValueArgs}"
    ${ARGN}
  )

  if(NOT DEFINED x_EXPORT)
    message(FATAL_ERROR "Export name is required.")
    return()
  endif()

  set(runtime "${META_MODULE_NAME}_runtime")
  set(dev "${META_MODULE_NAME}_dev")

  message(STATUS "[oxygen_module_install] Namespace: ${x_EXPORT}")
  message(STATUS "[oxygen_module_install] Targets: ${x_TARGETS}")
  message(STATUS "[oxygen_module_install] Runtime component: ${runtime}")
  message(STATUS "[oxygen_module_install] Dev component: ${dev}")

  install(
    TARGETS
      ${x_TARGETS}
    EXPORT "${x_EXPORT}"
    RUNTIME
      DESTINATION ${OXYGEN_INSTALL_BIN}
      COMPONENT ${runtime}
    LIBRARY
      DESTINATION ${OXYGEN_INSTALL_SHARED}
      COMPONENT ${runtime}
    ARCHIVE
      DESTINATION ${OXYGEN_INSTALL_LIB}
      COMPONENT ${dev}
    FILE_SET
    HEADERS
      DESTINATION ${OXYGEN_INSTALL_INCLUDE}/${x_INCLUDE_PREFIX}
      COMPONENT ${dev}
  )
  install(
    EXPORT ${x_EXPORT}
    NAMESPACE ${x_EXPORT}::
    DESTINATION ${OXYGEN_INSTALL_CMAKE}
    FILE "${x_EXPORT}-targets.cmake"
    COMPONENT Development
  )
endfunction()
