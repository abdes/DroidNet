# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

include(GTestHelpers)

add_subdirectory("from_main")
add_subdirectory("from_comp_lib")
if(NOT BUILD_SHARED_LIBS)
  add_subdirectory("from_cs_init")
endif()
