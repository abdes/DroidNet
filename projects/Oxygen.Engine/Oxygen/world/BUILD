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
    name = "types",
    hdrs = [
        "types.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        "//oxygen/base:resource_handle",
    ],
)

cc_library(
    name = "transform",
    hdrs = [
        "transform.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":types",
        "//oxygen/base:resource",
        "//oxygen/base:resource_handle",
        "@glm",
    ],
)

cc_library(
    name = "entity",
    srcs = [
        "entity.cpp",
    ],
    hdrs = [
        "entity.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":transform",
        ":types",
        "//oxygen/base:resource",
        "//oxygen/base:resource_handle",
        "//oxygen/base:resource_table",
    ],
)

cc_library(
    name = "world",
    deps = [
        ":entity",
        ":transform",
        ":types",
    ],
)

cc_test(
    name = "entity-component_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/entity_component_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    deps = [
        ":world",
        "@googletest//:gtest",
    ],
)
