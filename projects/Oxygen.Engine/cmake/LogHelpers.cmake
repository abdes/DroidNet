# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Only definitions are guarded. Stack state belongs to the caller's directory
# and is inherited by child directories without being reset on inclusion.
include_guard(GLOBAL)

function(_oxygen_log_change operation kind name)
  if(
    NOT
      ARGC
        EQUAL
        3
    OR
      name
        STREQUAL
        ""
    OR
      name
        MATCHES
        ";"
    OR
      name
        MATCHES
        "\\(|\\)|\\[|\\]"
  )
    message(
      FATAL_ERROR
      "Oxygen hierarchy: expected one nonempty ${kind} name without hierarchy delimiters."
    )
  endif()
  if(kind STREQUAL "project")
    set(entry "[${name}]")
  else()
    set(entry "(${name})")
  endif()
  set(stack "${ASAP_LOG_PROJECT_HIERARCHY_STACK}")
  if(operation STREQUAL "push")
    if(kind STREQUAL "module" AND NOT name STREQUAL META_MODULE_NAME)
      message(
        FATAL_ERROR
        "Oxygen hierarchy: declare module '${name}' before pushing it."
      )
    endif()
    list(APPEND stack "${entry}")
  else()
    list(LENGTH stack count)
    if(count EQUAL 0)
      message(
        FATAL_ERROR
        "Oxygen hierarchy: cannot pop ${kind} '${name}' from an empty stack."
      )
    endif()
    list(GET stack -1 top)
    if(NOT top STREQUAL entry)
      message(
        FATAL_ERROR
        "Oxygen hierarchy: cannot pop ${entry}; top of stack is ${top}."
      )
    endif()
    list(POP_BACK stack)
  endif()
  list(JOIN stack " > " hierarchy)
  list(LENGTH stack depth)
  if(operation STREQUAL "push")
    if(kind STREQUAL "project")
      if(OXYGEN_IS_MASTER_PROJECT)
        set(role "master")
      else()
        set(role "sub-project")
      endif()
      message(STATUS "=> [${depth}] in project ${hierarchy} (${role})")
    else()
      message(STATUS "=> [${depth}] in module ${hierarchy}")
      message(
        STATUS
        "   Target: ${META_MODULE_TARGET} - Alias: ${META_MODULE_TARGET_ALIAS}"
      )
    endif()
  elseif(depth GREATER 0)
    message(STATUS ".. [${depth}] back to ${hierarchy}")
  endif()
  set(ASAP_LOG_PROJECT_HIERARCHY_STACK "${stack}" PARENT_SCOPE)
  set(ASAP_LOG_PROJECT_HIERARCHY "${hierarchy}" PARENT_SCOPE)
endfunction()

function(asap_push_project name)
  if(NOT ARGC EQUAL 1)
    message(FATAL_ERROR "asap_push_project: expected exactly one name.")
  endif()
  _oxygen_log_change(push project "${name}")
  set(
    ASAP_LOG_PROJECT_HIERARCHY_STACK
    "${ASAP_LOG_PROJECT_HIERARCHY_STACK}"
    PARENT_SCOPE
  )
  set(ASAP_LOG_PROJECT_HIERARCHY "${ASAP_LOG_PROJECT_HIERARCHY}" PARENT_SCOPE)
endfunction()

function(asap_pop_project name)
  if(NOT ARGC EQUAL 1)
    message(FATAL_ERROR "asap_pop_project: expected exactly one name.")
  endif()
  _oxygen_log_change(pop project "${name}")
  set(
    ASAP_LOG_PROJECT_HIERARCHY_STACK
    "${ASAP_LOG_PROJECT_HIERARCHY_STACK}"
    PARENT_SCOPE
  )
  set(ASAP_LOG_PROJECT_HIERARCHY "${ASAP_LOG_PROJECT_HIERARCHY}" PARENT_SCOPE)
endfunction()

function(asap_push_module name)
  if(NOT ARGC EQUAL 1)
    message(FATAL_ERROR "asap_push_module: expected exactly one name.")
  endif()
  _oxygen_log_change(push module "${name}")
  set(
    ASAP_LOG_PROJECT_HIERARCHY_STACK
    "${ASAP_LOG_PROJECT_HIERARCHY_STACK}"
    PARENT_SCOPE
  )
  set(ASAP_LOG_PROJECT_HIERARCHY "${ASAP_LOG_PROJECT_HIERARCHY}" PARENT_SCOPE)
endfunction()

function(asap_pop_module name)
  if(NOT ARGC EQUAL 1)
    message(FATAL_ERROR "asap_pop_module: expected exactly one name.")
  endif()
  _oxygen_log_change(pop module "${name}")
  set(
    ASAP_LOG_PROJECT_HIERARCHY_STACK
    "${ASAP_LOG_PROJECT_HIERARCHY_STACK}"
    PARENT_SCOPE
  )
  set(ASAP_LOG_PROJECT_HIERARCHY "${ASAP_LOG_PROJECT_HIERARCHY}" PARENT_SCOPE)
endfunction()
