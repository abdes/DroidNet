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
    name = "version",
    srcs = [
        "version.cpp",
    ],
    hdrs = [
        "version.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        "//oxygen:info",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
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
    name = "core",
    srcs = [
        "engine.cpp",
    ],
    hdrs = [
        "engine.h",
        "input_handler.h",
        "module.h",
        "system.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":version",
        "//oxygen/base:time",
        "//oxygen/base:types",
        "//oxygen/logging",
        "//oxygen/platform",
        "//oxygen/platform:fwd",
        "@sigslot",
    ],
)

cc_test(
    name = "input_handler_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/input_handler_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":core",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "system_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/system_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":core",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "version_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/version_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":version",
        "//oxygen:info",
        "@googletest//:gtest",
    ],
)
