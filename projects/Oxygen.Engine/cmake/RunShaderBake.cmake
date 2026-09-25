# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# Included by a generated configuration-specific script. Separate build trees
# retain their own caches but must coordinate publication into the shared shader
# directory. The process guard also releases the lock on a failed invocation.
file(MAKE_DIRECTORY "${OXYGEN_SHADERBAKE_OUTPUT_DIR}")
file(LOCK "${OXYGEN_SHADERBAKE_OUTPUT_DIR}/.shaderbake.lock" GUARD PROCESS)

execute_process(
  COMMAND
    ${OXYGEN_SHADERBAKE_RUNTIME} "${OXYGEN_SHADERBAKE_EXECUTABLE}" update
    --workspace-root "${OXYGEN_SHADERBAKE_WORKSPACE}" --build-root
    "${OXYGEN_SHADERBAKE_CACHE}" --out "${OXYGEN_SHADERBAKE_OUTPUT}" --mode
    "${OXYGEN_SHADERBAKE_MODE}"
  RESULT_VARIABLE _result
)
if(NOT "${_result}" MATCHES "^[0-9]+$")
  message(FATAL_ERROR "ShaderBake could not run: ${_result}")
endif()
cmake_language(EXIT "${_result}")
