"""Oxygen specific copts.

This file simply selects the correct options from the generated files.  To
change Oxygen copts, edit <path_to_oxygen>/copts/copts.py
"""

load(
    "//oxygen:copts/GENERATED_copts.bzl",
    "OXYGEN_CLANG_CL_FLAGS",
    "OXYGEN_CLANG_CL_TEST_FLAGS",
    "OXYGEN_GCC_FLAGS",
    "OXYGEN_GCC_TEST_FLAGS",
    "OXYGEN_LLVM_FLAGS",
    "OXYGEN_LLVM_TEST_FLAGS",
    "OXYGEN_MSVC_FLAGS",
    "OXYGEN_MSVC_LINKOPTS",
    "OXYGEN_MSVC_TEST_FLAGS",
)

OXYGEN_DEFAULT_COPTS = select({
    "//oxygen:msvc_compiler": OXYGEN_MSVC_FLAGS,
    "//oxygen:clang-cl_compiler": OXYGEN_CLANG_CL_FLAGS,
    "//oxygen:clang_compiler": OXYGEN_LLVM_FLAGS,
    "//oxygen:gcc_compiler": OXYGEN_GCC_FLAGS,
    "//conditions:default": OXYGEN_GCC_FLAGS,
})

OXYGEN_TEST_COPTS = select({
    "//oxygen:msvc_compiler": OXYGEN_MSVC_TEST_FLAGS,
    "//oxygen:clang-cl_compiler": OXYGEN_CLANG_CL_TEST_FLAGS,
    "//oxygen:clang_compiler": OXYGEN_LLVM_TEST_FLAGS,
    "//oxygen:gcc_compiler": OXYGEN_GCC_TEST_FLAGS,
    "//conditions:default": OXYGEN_GCC_TEST_FLAGS,
})

OXYGEN_DEFAULT_LINKOPTS = select({
    "//oxygen:msvc_compiler": OXYGEN_MSVC_LINKOPTS,
    "//conditions:default": [],
})
