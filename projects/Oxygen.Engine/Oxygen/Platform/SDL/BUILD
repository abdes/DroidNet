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
    name = "wrapper",
    srcs = [
        "detail/wrapper.cpp",
        "detail/wrapper.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/platform:window",
        "@sdl3",
    ],
)

cc_library(
    name = "display",
    srcs = [
        "display.cpp",
    ],
    hdrs = [
        "display.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        ":wrapper",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/platform:display",
        "@sdl3",
    ],
)

cc_library(
    name = "window",
    srcs = [
        "window.cpp",
    ],
    hdrs = [
        "window.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        ":wrapper",
        "//oxygen/logging",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/platform:window",
        "@sdl3",
    ],
)

cc_library(
    name = "platform-sdl",
    srcs = [
        "event_utils.cpp",
        "platform.cpp",
    ],
    hdrs = [
        "platform.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        ":wrapper",
        "//oxygen/base:types",
        "//oxygen/logging",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":display",
        ":window",
        "//oxygen/base:config",
        "//oxygen/base:macros",
        "//oxygen/platform",
        "@fmt",
        "@sdl3",
        "@sigslot",
    ],
)
