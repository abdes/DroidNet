# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

load(
    "//oxygen:copts/configure_copts.bzl",
    "OXYGEN_DEFAULT_COPTS",
    "OXYGEN_DEFAULT_LINKOPTS",
    "OXYGEN_TEST_COPTS",
)

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "fwd",
    hdrs = [
        "fwd.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
)

cc_library(
    name = "input",
    srcs = [
        "action.cpp",
        "action_triggers.cpp",
        "input_action_mapping.cpp",
        "input_mapping_context.cpp",
        "input_system.cpp",
    ],
    hdrs = [
        "action.h",
        "action_triggers.h",
        "action_value.h",
        "input_action_mapping.h",
        "input_mapping_context.h",
        "input_system.h",
        "types.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        "//oxygen/core",
        "//oxygen/logging",
        "//oxygen/platform:fwd",
        "//oxygen/platform:input",
        "//oxygen/platform:types",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/base:types",
        "@sigslot",
    ],
)
