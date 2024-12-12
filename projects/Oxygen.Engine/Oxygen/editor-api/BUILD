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

cc_shared_library(
    name = "editor-api",
    deps = [":_engine"],
)

cc_library(
    name = "_engine",
    srcs = [
        "api.cpp",
        "dllmain.cpp",
    ],
    hdrs = [
        "api.h",
        "api_export.h",
    ],
    copts = OXYGEN_DEFAULT_COPTS,
    implementation_deps = [
        "//oxygen/base:resource",
        "//oxygen/world",
    ],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    local_defines = ["OXYGEN_ENGINE_EXPORTS"],
    # This target is only built for windows
    target_compatible_with = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    visibility = ["//visibility:__pkg__"],
    deps = [
        "//oxygen/base:resource_handle",
        "@glm",
    ],
)

cc_test(
    name = "loaded_editor_api_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/loaded_editor_api_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    # This target is only built for windows
    target_compatible_with = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    deps = [
        ":_engine",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "linked_editor_api_test",
    size = "small",  # Other options: "medium", "large", "enormous"
    srcs = [
        "test/linked_editor_api_test.cpp",
        "test/main.cpp",
    ],
    copts = OXYGEN_TEST_COPTS,
    dynamic_deps = [":editor-api"],
    linkopts = OXYGEN_DEFAULT_LINKOPTS,
    # This target is only built for windows
    target_compatible_with = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    deps = [
        ":_engine",
        "@googletest//:gtest",
    ],
)
