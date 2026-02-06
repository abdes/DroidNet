# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import os
import json
from typing import Any, cast
from conan import ConanFile  # type: ignore
from conan.tools.cmake import CMakeToolchain, CMakeDeps  # type: ignore
from conan.tools.files import load, copy  # type: ignore
from conan.tools.cmake import cmake_layout, CMake  # type: ignore
from conan.tools.microsoft import is_msvc_static_runtime, is_msvc  # type: ignore
from conan.errors import ConanInvalidConfiguration  # type: ignore
from pathlib import Path


class OxygenConan(ConanFile):
    deploy_folder: str  # let Pyright know this exists
    # Common Conan dynamic attributes annotated to satisfy static checkers
    output: Any
    settings: Any
    cpp: Any
    conf: Any
    folders: Any
    dependencies: Any
    recipe_folder: Any

    # Reference
    name = "Oxygen"

    # Metadata
    description = "Oxygen Game Engine."
    license = "BSD 3-Clause License"
    homepage = "https://github.com/abdes/oxygen"
    url = "https://github.com/abdes/oxygen/"
    topics = ("graphics programming", "gamedev", "math")

    # Binary model: Settings and Options
    # Include `sanitizer` so profiles can control sanitizer across the
    # whole dependency graph and it is part of package identity.
    settings = "os", "arch", "compiler", "build_type", "sanitizer"
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
        self.requires("ftxui/6.1.9")
        self.requires("libspng/0.7.4")

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
        sanitizer = self.settings.get_safe("sanitizer")
        if sanitizer == "asan":
            # Do not reassign recipe options here. If the global
            # `sanitizer` setting is present but the `with_asan` option
            # is not enabled, log a clear warning so users can fix their
            # profiles. This keeps behavior explicit and avoids Conan
            # errors about modifying fixed options.
            if not bool(getattr(self.options, "with_asan", False)):
                self.output.warning(
                    "Profile sets sanitizer=asan; please set "
                    "Oxygen/*:with_asan=True in your profile to enable ASAN"
                )

        if self.options.shared:
            self.options.rm_safe("fPIC")
            # When building shared libs, and compiler is MSVC, we need to set
            # the runtime to dynamic
            if is_msvc(self) and is_msvc_static_runtime(self):
                self.output.error(
                    "Should not build shared libraries with static runtime!"
                )
                raise Exception("Invalid build configuration")

        # Link to test frameworks always as static libs (guard if not present)
        try:
            self.options["gtest"].shared = False
        except Exception:
            # gtest may not be present in this configuration
            pass
        try:
            self.options["catch2"].shared = False
        except Exception:
            # catch2 may not be present in this configuration
            pass

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

    def validate(self):
        if self._with_asan and self.settings.build_type != "Debug":
            raise ConanInvalidConfiguration(
                "ASan is only supported for Debug builds. "
                f"Current build_type is {self.settings.build_type}."
            )

    @property
    def _is_ninja(self):
        """Identify if Ninja (Multi-Config) is requested via conf or environment."""
        gen = self.conf.get("tools.cmake.cmaketoolchain:generator", default="")
        return "Ninja" in str(gen) or (not gen and "VSCODE_PID" in os.environ)

    @property
    def _with_asan(self):
        """Determine if ASAN is enabled via settings or options."""
        if self.settings.get_safe("sanitizer") == "asan":
            return True
        try:
            return bool(self.options.get_safe("with_asan"))
        except Exception:
            return False

    @property
    def _install_subfolder(self):
        """Determine the subfolder for deployment/installation: (Debug, Release, Asan)."""
        return "Asan" if self._with_asan else str(self.settings.build_type)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.absolute_paths = True
        # Use distinct preset names to allow simultaneous builds without collisions
        # Match the exact MixedCase naming used in tools/presets/BasePresets.json
        tc.user_presets_path = (
            "ConanPresets-Ninja.json"
            if self._is_ninja
            else "ConanPresets-VS.json"
        )

        self._set_cmake_defs(tc.variables)
        if is_msvc(self):
            tc.variables["USE_MSVC_RUNTIME_LIBRARY_DLL"] = (
                not is_msvc_static_runtime(self)
            )

        # Set OXYGEN_CONAN_DEPLOY_DIR to the base install directory.
        # CMakeLists.txt will append the configuration (Debug, Release, Asan)
        # to form the actual CMAKE_INSTALL_PREFIX.
        install_base = str(
            Path(cast(str, self.recipe_folder)) / "out" / "install"
        )
        tc.variables["OXYGEN_CONAN_DEPLOY_DIR"] = install_base.replace(
            "\\", "/"
        )

        # Restored ASAN logic
        # Only enable ASan when the explicit option `with_asan` is set.
        # Do not fall back to the `sanitizer` setting to avoid
        # unpredictable behavior from implicit profile values.
        enable_asan = self._with_asan
        tc.variables["OXYGEN_WITH_ASAN"] = "ON" if enable_asan else "OFF"

        if self.options.with_coverage:
            tc.cache_variables["OXYGEN_WITH_COVERAGE"] = "ON"

        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

        # When ASan builds are requested, augment the generated CMakePresets
        # by adding '-asan' variants of the generated presets so users can
        # select ASan-specific presets without replacing Conan's originals.
        if self._with_asan:
            try:
                self._append_asan_to_presets()
            except Exception as e:
                # Don't fail the generation step if post-processing fails
                self.output.warning(f"Failed to append -asan presets: {e}")

    def _append_asan_to_presets(self):
        """Duplicate generated presets appending '-asan' to their names.

        This creates copies of configure, build and test presets with
        an '-asan' suffix and updates configurePreset/inherits references
        inside the duplicated entries so they point to each other.
        This is intentionally non-destructive (duplicates rather than
        renames) to avoid breaking repo presets that may inherit the
        original Conan-generated preset names.
        """
        build_dir = getattr(self.folders, "build", None)

        # Build folder may not yet be available during `generate()`. Try multiple
        # candidate locations (explicit build_dir first, then the expected
        # repo-relative build path used in `layout()`). Log diagnostics to aid
        # debugging when post-processing is skipped.
        candidates = []
        if build_dir:
            candidates.append(Path(build_dir))

        suffix = "ninja" if self._is_ninja else "vs"
        if self._with_asan:
            suffix = f"asan-{suffix}"
        expected = Path(self.recipe_folder) / f"out/build-{suffix}"
        candidates.append(expected)

        found = None
        for cand in candidates:
            presets_path_obj = cand / "generators" / "CMakePresets.json"
            # Use info so it is visible in normal logs; keep concise.
            self.output.info(f"Looking for CMakePresets at {presets_path_obj}")
            if presets_path_obj.exists():
                found = presets_path_obj
                break

        if not found:
            self.output.info(
                "Presets not found in candidate locations; skipping -asan augmentation"
            )
            return

        presets_path = str(found)

        with open(presets_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        # Rename generated presets in-place to append '-asan' to every preset name
        # This removes the original non-ASan preset names entirely so only ASan
        # variants exist in the generated file.
        name_map = {}

        cfgs = data.get("configurePresets", [])

        # First pass: compute renames and apply to configure presets
        for p in cfgs:
            name = p.get("name")
            if not name or name.endswith("-asan"):
                continue
            new_name = name + "-asan"
            name_map[name] = new_name
            p["name"] = new_name
            if "displayName" in p:
                p["displayName"] = p["displayName"].replace(name, new_name)
            else:
                p["displayName"] = f"'{new_name}' config"
            if "description" in p and "ASan" not in p["description"]:
                p["description"] = p["description"] + " (ASan)"

        # Second pass: update 'inherits' references among configure presets
        for p in cfgs:
            inherits = p.get("inherits")
            if inherits and inherits in name_map:
                p["inherits"] = name_map[inherits]

        # Update build and test presets to reference the renamed configure presets
        for section in ("buildPresets", "testPresets"):
            presets = data.get(section, [])
            for p in presets:
                cfg = p.get("configurePreset")
                if cfg and cfg in name_map:
                    p["configurePreset"] = name_map[cfg]
                # Rename the build/test preset itself to have '-asan' suffix as well
                pname = p.get("name")
                if pname and not pname.endswith("-asan"):
                    p["name"] = pname + "-asan"

        # Write back the modified presets file
        with open(presets_path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=4)

        self.output.info(
            f"Renamed presets to '-asan' variants in {presets_path}"
        )

    def layout(self):
        # Dynamically set build folder based on the generator and ASAN status
        suffix = "ninja" if self._is_ninja else "vs"
        if self._with_asan:
            suffix = f"asan-{suffix}"

        build_folder = f"out/build-{suffix}"
        cmake_layout(self, build_folder=build_folder)

        # Ensure generated headers are available to the build
        self.cpp.build.includedirs.append(
            os.path.join(self.folders.build, "include")
        )

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

        # Determine the target subfolder for deployment using the common logic
        target_deploy_folder = os.path.join(
            self.deploy_folder, self._install_subfolder
        )

        def try_copy(patterns, src, dst, pkg_name):
            for pattern in patterns:
                try:
                    copy(self, pattern, src=src, dst=dst)
                except Exception as e:
                    self.output.error(
                        f"Failed copying {pattern} from {pkg_name}: {e}"
                    )
                    raise

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
            for incdir in getattr(dep.cpp_info, "includedirs", []) or []:
                try:
                    copy(
                        self,
                        "*",
                        src=incdir,
                        dst=os.path.join(target_deploy_folder, "include"),
                    )
                except Exception as e:
                    self.output.error(
                        f"Failed copying headers from {name}: {e}"
                    )
                    raise

            # Static/import libs go to lib; shared libs may be in libdirs but belong in bin
            for libdir in getattr(dep.cpp_info, "libdirs", []) or []:
                try_copy(
                    ["*.lib", "*.a"],
                    libdir,
                    os.path.join(target_deploy_folder, "lib"),
                    name,
                )
                try_copy(
                    ["*.dll", "*.so*", "*.dylib*"],
                    libdir,
                    os.path.join(target_deploy_folder, "bin"),
                    name,
                )

            # Executables + shared objects in bindirs
            for bindir in getattr(dep.cpp_info, "bindirs", []) or []:
                try_copy(
                    ["*.exe", "*.dll", "*.so*", "*.dylib*"],
                    bindir,
                    os.path.join(target_deploy_folder, "bin"),
                    name,
                )
