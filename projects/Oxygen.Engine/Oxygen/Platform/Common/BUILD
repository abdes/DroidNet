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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "types",
    hdrs = [
        "types.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/base:types",
    ],
)

cc_library(
    name = "input",
    srcs = [
        "input.cpp",
    ],
    hdrs = [
        "input.h",
        "input_event.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        "//oxygen/logging",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
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
        "//oxygen/base:config",
        "@fmt",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/base:macros",
        "//oxygen/base:types",
    ],
)

cc_library(
    name = "window",
    srcs = [
        "detail/base_window.cpp",
        "detail/base_window.h",
        "window.cpp",
    ],
    hdrs = [
        "window.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        "//oxygen/logging",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
        "//oxygen/base:macros",
        "@sigslot",
    ],
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
    name = "platform",
    srcs = [
        "platform.cpp",
    ],
    hdrs = [
        "platform.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":display",
        ":input",
        ":types",
        ":window",
        "//oxygen/base:macros",
        "@sigslot",
    ],
)

cc_test(
    name = "input_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/input_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":input",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "window_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/window_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":window",
        "@googletest//:gtest",
    ],
)
