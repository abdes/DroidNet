# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

load(
    "//oxygen:copts/configure_copts.bzl",
    "OXYGEN_DEFAULT_COPTS",
    "OXYGEN_DEFAULT_LINKOPTS",
)

package(
    default_visibility = ["//visibility:public"],
)

cc_binary(
    name = "input-system",
    srcs = [
        "main.cpp",
        "main_module.cpp",
        "main_module.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/base:config",
        "//oxygen/core",
        "//oxygen/input",
        "//oxygen/input:fwd",
        "//oxygen/platform-sdl",
        "@sdl3",
    ],
)

cc_binary(
    name = "print-sdl-code-names",
    srcs = ["print_sdl_code_names.cpp"],
    deps = ["@sdl3"],
)
