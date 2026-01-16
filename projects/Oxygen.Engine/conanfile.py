# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import os
from typing import Any
from conan import ConanFile  # type: ignore
from conan.tools.cmake import CMakeToolchain, CMakeDeps  # type: ignore
from conan.tools.files import load, copy  # type: ignore
from conan.tools.cmake import cmake_layout, CMake  # type: ignore
from conan.tools.microsoft import is_msvc_static_runtime, is_msvc  # type: ignore
from pathlib import Path


class OxygenConan(ConanFile):
    deploy_folder: str  # let Pyright know this exists

    # Reference
    name = "Oxygen"

    # Metadata
    description = "Oxygen Game Engine."
    license = "BSD 3-Clause License"
    homepage = "https://github.com/abdes/oxygen"
    url = "https://github.com/abdes/oxygen/"
    topics = ("graphics programming", "gamedev", "math")

    # Binary model: Settings and Options
    settings = "os", "arch", "compiler", "build_type"
    options: Any = {
        # Options
        "shared": [True, False],
        "fPIC": [True, False],
        "awaitable_state_checker": [True, False],
        "with_asan": [True, False],
        "with_coverage": [True, False],
        # Optional components:
        "base": [True, False],
        "oxco": [True, False],
        # Also build and install:
        "tools": [True, False],
        "examples": [True, False],
        "tests": [True, False],
        "benchmarks": [True, False],
        "docs": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "awaitable_state_checker": True,
        "with_asan": False,
        "with_coverage": False,
        # Optional components:
        "base": True,
        "oxco": True,
        # Also build and package:
        "tools": True,
        "examples": True,
        "tests": True,
        "benchmarks": True,
        "docs": True,
        # Dependencies options:
        "fmt/*:header_only": True,
        "sdl/*:shared": True,
    }

    exports_sources = (
        "VERSION",
        "AUTHORS",
        "README.md",
        "LICENSE",
        "CMakeLists.txt",
        ".clangd.in",
        "cmake/**",
        "src/**",
        "Examples/**",
        "tools/**",
        "!out/**",
        "!build/**",
        "!cmake-build-*/**",
    )

    def set_version(self):
        assert (
            self.recipe_folder is not None
        ), "recipe_folder must be set before set_version()"
        self.version = load(self, Path(self.recipe_folder) / "VERSION").strip()

    def requirements(self):
        self.requires("fmt/12.1.0")
        self.requires("sdl/3.2.28")
        self.requires("imgui/1.92.5")
        self.requires("asio/1.36.0")
        self.requires("glm/1.0.1")
        self.requires("nlohmann_json/3.11.3")
        self.requires("json-schema-validator/2.4.0")
        self.requires("stduuid/1.2.3")
        self.requires("magic_enum/0.9.7")
        self.requires("tinyexr/1.0.12")
        self.requires("pdcurses/3.9")

        # Record test-only dependencies so we can skip them during deploy.
        # The test_requires call accepts a reference like 'gtest/master'.
        self._test_deps = set()
        ref = "gtest/master"  # google test recommends using 'master'
        self.test_requires(ref)
        self._test_deps.add(ref.split("/")[0])

        ref = "benchmark/1.9.4"
        self.test_requires(ref)
        self._test_deps.add(ref.split("/")[0])

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
            # When building shared libs, and compiler is MSVC, we need to set
            # the runtime to dynamic
            if is_msvc(self) and is_msvc_static_runtime(self):
                self.output.error(
                    "Should not build shared libraries with static runtime!"
                )
                raise Exception("Invalid build configuration")

        # Link to test frameworks always as static libs
        self.options["gtest"].shared = False
        self.options["catch2"].shared = False

        # Enable tinyexr to build with threading and OpenMP support when available
        try:
            self.options["tinyexr"].with_thread = True
            self.options["tinyexr"].with_openmp = True
        except Exception:
            # If tinyexr isn't present in this configuration, ignore silently
            pass

        # Enable wide-character support for pdcurses when available
        try:
            self.options["pdcurses"].enable_widec = True
        except Exception:
            # If pdcurses isn't present in this configuration, ignore silently
            pass

    def generate(self):
        tc = CMakeToolchain(self)

        # We want to use conan generated presets, but we want to have our own
        # ones too. Specify the path of ConanPresets so that it's not put in
        # place where we cannot `include` it.
        tc.absolute_paths = True
        tc.user_presets_path = "ConanPresets.json"

        self._set_cmake_defs(tc.variables)
        if is_msvc(self):
            tc.variables["USE_MSVC_RUNTIME_LIBRARY_DLL"] = (
                not is_msvc_static_runtime(self)
            )

        if self.options.with_asan:
            tc.cache_variables["OXYGEN_WITH_ASAN"] = "ON"

        # Check if we need to enable COVERAGE
        if self.options.with_coverage:
            tc.cache_variables["OXYGEN_WITH_COVERAGE"] = "ON"

        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def layout(self):
        # Use the cmake layout with a custom build dir,
        cmake_layout(self, build_folder="out/build")
        # and add the generated headers to the includedirs
        generated_headers = os.path.join(self.folders.build, "include")
        build_info: Any = self.cpp.build  # type: ignore
        build_info.includedirs.append(generated_headers)

    def _set_cmake_defs(self, defs):
        defs["OXYGEN_BUILD_TOOLS"] = self.options.tools
        defs["OXYGEN_BUILD_EXAMPLES"] = self.options.examples
        defs["OXYGEN_BUILD_TESTS"] = self.options.tests
        defs["OXYGEN_BUILD_BENCHMARKS"] = self.options.benchmarks
        defs["OXYGEN_BUILD_DOCS"] = self.options.docs
        defs["BUILD_SHARED_LIBS"] = self.options.shared

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        cmake = CMake(self)
        cmake.test()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def _library_name(self, component: str):
        # Split the string into segments
        segments = component.split("-")
        # Capitalize the first letter of each segment (except the first one)
        lib_name = "Oxygen." + ".".join(word.capitalize() for word in segments)
        self.output.debug(
            f"Library for component '{component}' is '{lib_name}'"
        )
        return lib_name

    def package_info(self):
        for name in ["OxCo", "Base"]:
            if not self.options.get_safe(name.lower(), True):
                continue  # component is disabled

            component = self.cpp_info.components["oxygen-" + name]
            component.libs = []
            component.libdirs = []
            component.set_property(
                "cmake_target_name", "oxygen-" + name.lower()
            )
            component.set_property(
                "cmake_target_aliases", ["oxygen::" + name.lower()]
            )

        # Define Base component (compiled library)
        if self.options.get_safe("base", True):
            base = self.cpp_info.components["oxygen-Base"]
            base.libs = [self._library_name("Base")]
            base.libdirs = ["lib"]
            # Expose CMake target and alias oxygen::base for consumers

        # Define OxCo component (header-only/meta; depends on Base)
        if self.options.get_safe("oxco", True):
            oxco = self.cpp_info.components["oxygen-OxCo"]
            oxco.includedirs = ["include"]
            oxco.builddirs = ["lib/cmake/oxygen"]
            oxco.libs = []
            oxco.libdirs = []
            # Internal dependency on Base component when available
            if self.options.get_safe("base", True):
                oxco.requires = ["oxygen-Base"]

    # def build_requirements(self):
    #     self.build_requires("cmake/[>=3.25.0]")
    #     self.build_requires("ninja/[>=1.11.0]")

    def deploy(self):
        test_deps = getattr(self, "_test_deps", set())
        for dep in self.dependencies.values():
            # Derive a safe package name (ref may be None for some deps)
            try:
                dep_name = dep.ref.name if dep.ref is not None else None
            except Exception:
                dep_name = None
            # Skip test-only dependencies during deploy
            if dep_name in test_deps:
                continue
            if dep_name:
                name = dep_name
            else:
                # Fallback to the package folder basename if ref is not present
                try:
                    name = os.path.basename(dep.package_folder)
                except Exception:
                    name = "unknown"

            # ---- Headers (namespaced per package) ----
            for incdir in dep.cpp_info.includedirs:

                copy(
                    self,
                    "*",
                    src=incdir,
                    dst=os.path.join(self.deploy_folder, "include"),
                )

            # Static + import libs
            for libdir in dep.cpp_info.libdirs:
                copy(
                    self,
                    "*.lib",
                    src=libdir,
                    dst=os.path.join(self.deploy_folder, "lib"),
                )
                copy(
                    self,
                    "*.a",
                    src=libdir,
                    dst=os.path.join(self.deploy_folder, "lib"),
                )
                # DLLs sometimes land here too
                copy(
                    self,
                    "*.dll",
                    src=libdir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
                copy(
                    self,
                    "*.so*",
                    src=libdir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
                copy(
                    self,
                    "*.dylib*",
                    src=libdir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )

            # Executables + DLLs in bindirs
            for bindir in dep.cpp_info.bindirs:
                copy(
                    self,
                    "*.exe",
                    src=bindir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
                copy(
                    self,
                    "*.dll",
                    src=bindir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
                copy(
                    self,
                    "*.so*",
                    src=bindir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
                copy(
                    self,
                    "*.dylib*",
                    src=bindir,
                    dst=os.path.join(self.deploy_folder, "bin"),
                )
