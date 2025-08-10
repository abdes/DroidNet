# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

add_subdirectory("PakDump")

# === PakGen Python Tool Installation (Editable) ------------------------------#

# Opt-in target to install the PakGen Python package in editable mode so the
# `pakgen` CLI is available during development / build workflows. The install
# is skipped if the stamp file exists and the pyproject.toml has not changed.

option(OXYGEN_ENABLE_PAKGEN "Install PakGen python package (editable)" ON)
# Auto-enable when tests are enabled because C++ tests invoke the tool.
if(OXYGEN_BUILD_TESTS AND NOT DEFINED CACHE{OXYGEN_ENABLE_PAKGEN})
	set(OXYGEN_ENABLE_PAKGEN ON CACHE BOOL "Install PakGen python package (editable)" FORCE)
endif()

if(OXYGEN_ENABLE_PAKGEN)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	set(_pakgen_dir "${CMAKE_CURRENT_SOURCE_DIR}/PakGen")
	set(_pakgen_stamp "${CMAKE_BINARY_DIR}/pakgen_editable_install.stamp")
	set(_pakgen_pyproject "${_pakgen_dir}/pyproject.toml")

	add_custom_command(
		OUTPUT "${_pakgen_stamp}"
		COMMAND ${CMAKE_COMMAND} -E echo "[PakGen] Editable install starting"
		COMMAND ${CMAKE_COMMAND} -E env PIP_DISABLE_PIP_VERSION_CHECK=1
						${Python3_EXECUTABLE} -m pip install -e .[yaml]
		COMMAND ${CMAKE_COMMAND} -E touch "${_pakgen_stamp}"
		WORKING_DIRECTORY "${_pakgen_dir}"
		DEPENDS "${_pakgen_pyproject}"
		COMMENT "Installing PakGen (editable)"
		VERBATIM
	)

	add_custom_target(pakgen_editable_install ALL
		DEPENDS "${_pakgen_stamp}"
	)

	# Provide a convenient alias target for manual invocation without forcing ALL
	add_custom_target(pakgen_install DEPENDS pakgen_editable_install)
endif()
