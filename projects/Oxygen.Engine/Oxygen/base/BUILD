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
    name = "config",
    hdrs = [
        "compilers.h",
        "platform.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
)

cc_library(
    name = "types",
    hdrs = [
        "types.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
)

cc_library(
    name = "macros",
    hdrs = [
        "macros.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
)

cc_library(
    name = "resource_handle",
    hdrs = [
        "resource_handle.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
    ],
)

cc_library(
    name = "resource_table",
    hdrs = [
        "resource_table.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":macros",
        ":resource_handle",
    ],
)

cc_library(
    name = "resource",
    hdrs = [
        "resource.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":resource_handle",
    ],
)

cc_library(
    name = "time",
    hdrs = [
        "time.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
    ],
)

cc_library(
    name = "base",
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":config",
        ":macros",
        ":resource",
        ":resource_handle",
        ":resource_table",
        ":time",
        ":types",
    ],
)

cc_test(
    name = "macros_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/macros_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":macros",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "platform_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/platform_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":config",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "resource_handle_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/resource_handle_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":resource_handle",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "resource_table_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/resource_table_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":resource_table",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "time_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/time_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":time",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "types_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
        "test/types_test.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "sigslot_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "@googletest//:gtest",
        "@sigslot",
    ],
)
