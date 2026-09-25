# Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
# SPDX-License-Identifier: BSD-3-Clause

# These requirements apply to the full engine, not to independently reused
# portable source modules. Include after project() identifies the toolchain.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "Oxygen's full-engine build requires Windows x64.")
endif()

if(
  NOT
    CMAKE_CXX_COMPILER_ID
      STREQUAL
      "MSVC"
  OR
    CMAKE_CXX_COMPILER_VERSION
      VERSION_LESS
      "19.50"
)
  message(
    FATAL_ERROR
    "Oxygen requires MSVC 19.50 or newer (Visual Studio 2026 toolset v145). "
    "Detected ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}."
  )
endif()

set(
  _oxygen_supported_generators
  "Ninja"
  "Ninja Multi-Config"
  "Visual Studio 18 2026"
)
if(NOT CMAKE_GENERATOR IN_LIST _oxygen_supported_generators)
  message(
    FATAL_ERROR
    "Unsupported Oxygen generator '${CMAKE_GENERATOR}'. "
    "Use Ninja, Ninja Multi-Config, or Visual Studio 18 2026."
  )
endif()

if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(
    FATAL_ERROR
    "Oxygen requires a compiler with C++23 language-mode support."
  )
endif()

# Pointer size alone also accepts ARM64. Check the compiler's actual target,
# independent of the host processor and of generator-specific platform names.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
  [[
#if !defined(_M_X64) || defined(_M_ARM64EC)
#error Oxygen requires an x64 target.
#endif
int main() { return 0; }
]]
  OXYGEN_COMPILER_TARGETS_X64
)
if(NOT OXYGEN_COMPILER_TARGETS_X64)
  message(FATAL_ERROR "Oxygen's full-engine build requires an MSVC x64 target.")
endif()
