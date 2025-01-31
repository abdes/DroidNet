# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

load("@bazel_skylib//lib:selects.bzl", "selects")
load(
    "//oxygen:copts/configure_copts.bzl",
    "OXYGEN_DEFAULT_COPTS",
    "OXYGEN_DEFAULT_LINKOPTS",
    "OXYGEN_TEST_COPTS",
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # BSD 3-Clause License

# Expose license for external usage through bazel.
exports_files([
    "AUTHORS",
    "LICENSE",
])

config_setting(
    name = "msvc_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "msvc-cl",
    },
)

config_setting(
    name = "clang-cl_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "clang-cl",
    },
)

config_setting(
    name = "clang_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "clang",
    },
)

config_setting(
    name = "gcc_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "gcc",
    },
)

config_setting(
    name = "osx",
    constraint_values = [
        "@platforms//os:osx",
    ],
)

cc_library(
    name = "info",
    hdrs = ["version-info.h"],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
)
