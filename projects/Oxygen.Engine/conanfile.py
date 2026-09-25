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
from conan.tools.build import check_min_cppstd  # type: ignore
from conan.tools.scm import Version  # type: ignore
from pathlib import Path


required_conan_version = ">=2.32"


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
        "with_tracy": [True, False],
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
        "with_tracy": False,
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
        "CMakePresets.json",
        ".clangd",
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
        self.requires("magic_enum/0.9.7")
        self.requires("tinyexr/1.0.12")
        self.requires("pdcurses/3.9")
        self.requires("ftxui/6.1.9")
        self.requires("libspng/0.7.4")
        self.requires("luau/0.739")
        self.requires("joltphysics/5.5.0")
        self.requires("xxhash/0.8.3")
        self.requires("tracy/0.13.1")

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

        # Configure Tracy based on the with_tracy option
        if "tracy" in self.options:
            self.options["tracy"].enable = self.options.get_safe("with_tracy", False)
            self.options["tracy"].shared = self.options.get_safe("shared", False)

    def validate(self):
        if self.settings.os != "Windows" or self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration(
                "Oxygen's full-engine build requires Windows x64."
            )
        if (
            self.settings.compiler != "msvc"
            or Version(str(self.settings.compiler.version)) < "195"
        ):
            raise ConanInvalidConfiguration(
                "Oxygen requires MSVC 19.50 or newer "
                "(Conan compiler=msvc, compiler.version>=195)."
            )
        check_min_cppstd(self, 23)

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

    @property
    def _build_variant(self):
        """Use one identity for the build directory and its preset namespace."""
        parts = []
        if self.options.get_safe("with_tracy", False):
            parts.append("tracy")
        if self._with_asan:
            parts.append("asan")
        parts.append("ninja" if self._is_ninja else "vs")
        return "-".join(parts)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.absolute_paths = True
        tc.presets_prefix = f"conan-{self._build_variant}"
        # Oxygen owns the user presets that combine project policy with Conan's
        # generated presets. Leave Conan's per-tree files entirely native.
        tc.user_presets_path = False

        # Preset configuration deliberately reapplies all recipe defaults.
        # Keep graph expectations separate from overridable local build choices.
        self._set_cmake_defs(tc.cache_variables)
        tc.cache_variables["OXYGEN_WITH_ASAN"] = self._with_asan
        tc.cache_variables["OXYGEN_WITH_COVERAGE"] = bool(self.options.with_coverage)
        tc.cache_variables["OXYGEN_WITH_TRACY"] = bool(self.options.with_tracy)
        expectations = dict(tc.cache_variables)
        package_build = bool(self.package_folder)

        class OxygenOptionsBlock:
            # Graph facts must not be cached: every configure reads the current
            # install's expectations, even after recipe options have changed.
            template = """
{% for name, value in options.items() %}
set(OXYGEN_CONAN_EXPECT_{{ name }} {{ 'ON' if value else 'OFF' }})
{% endfor %}
set(OXYGEN_CONAN_PACKAGE_BUILD {{ 'ON' if package_build else 'OFF' }})
"""

            def context(self):
                return {"options": expectations, "package_build": package_build}

        tc.blocks["oxygen_options"] = OxygenOptionsBlock
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

        self._reset_legacy_presets(tc.presets_prefix)
        tc.generate()
        self._generate_project_presets()

        deps = CMakeDeps(self)
        deps.generate()

    def _generate_project_presets(self):
        """Derive project presets only for installed trees and configurations.

        CMakeUserPresets implicitly includes CMakePresets, so ordinary CMake
        inheritance joins the root policy and native Conan metadata without
        editing either. Missing trees never need placeholder presets.
        """
        if not self.source_folder or self.source_folder == self.generators_folder:
            return
        path = Path(self.source_folder) / "CMakeUserPresets.json"
        owner = "oxygenengine.org/presets/1.0"
        previous = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        if previous and not ({owner, "conan"} & previous.get("vendor", {}).keys()):
            raise ConanInvalidConfiguration(
                f"{path} is not generated by Oxygen or Conan; refusing to overwrite it."
            )
        # Preserve the platform of other installed trees when regenerating one.
        platforms = previous.get("vendor", {}).get(owner, {}).get("platforms", {})
        platform_defaults = {
            "Windows": "oxygen-windows-defaults",
            "Linux": "oxygen-posix-defaults",
            "Macos": "oxygen-posix-defaults",
        }.get(str(self.settings.os), "oxygen-configure-defaults")
        current = (Path(self.generators_folder) / "CMakePresets.json").resolve()
        includes = {current}
        includes.update((path.parent / p).resolve() for p in previous.get("include", []))
        data = {"version": 9, "vendor": {owner: {"platforms": {}}}, "include": [],
                "configurePresets": [], "buildPresets": [], "testPresets": []}
        for included in sorted(includes):
            if not included.is_file():
                continue
            native = json.loads(included.read_text(encoding="utf-8"))
            if "conan" not in native.get("vendor", {}):
                raise ConanInvalidConfiguration(f"Expected Conan presets in {included}")
            data["include"].append(included.as_posix())
            configure = native["configurePresets"][0]
            name = configure["name"]
            defaults = platform_defaults if included == current else platforms.get(name, platform_defaults)
            data["vendor"][owner]["platforms"][name] = defaults
            project_name = name.replace("conan-", "oxygen-", 1)
            data["configurePresets"].append({
                "name": project_name, "inherits": [defaults, name],
                "displayName": project_name.removeprefix("oxygen-").removesuffix("-default"),
            })
            for section in ("buildPresets", "testPresets"):
                for preset in native.get(section, []):
                    parents = [preset["name"]]
                    if section == "buildPresets":
                        parents.insert(0, "oxygen-build-defaults")
                    elif preset.get("configuration") in ("Debug", "Release"):
                        parents.insert(0, "oxygen-test-debug-defaults" if preset["configuration"] == "Debug"
                                       else "oxygen-test-defaults")
                    data[section].append({
                        "name": preset["name"].replace("conan-", "oxygen-", 1),
                        "inherits": parents, "configurePreset": project_name,
                    })
        temporary = path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
        temporary.replace(path)

    def _reset_legacy_presets(self, prefix):
        """Let Conan regenerate legacy metadata on the first install.

        Conan merges multi-config build/test entries by name. Retaining the old
        names would leave colliding entries pointing at removed configure presets.
        Also discard metadata changed by the former defaults postprocessor.
        Only Conan-owned preset metadata is removed; build products stay intact.
        """
        path = Path(self.generators_folder) / "CMakePresets.json"
        if not path.exists():
            return
        data = json.loads(path.read_text(encoding="utf-8"))
        if "conan" not in data.get("vendor", {}):
            return  # Conan reports the ownership error without overwriting it.
        presets = (p for section in ("configurePresets", "buildPresets", "testPresets")
                   for p in data.get(section, []))
        if (data.get("include")
                or any(not p["name"].startswith(prefix + "-") or "inherits" in p for p in presets)):
            path.unlink()
            self.output.info(f"Regenerating legacy presets with prefix '{prefix}'")

    def layout(self):
        cmake_layout(self, build_folder=f"out/build-{self._build_variant}")

        # Ensure generated headers are available to the build
        self.cpp.build.includedirs.append(
            os.path.join(self.folders.build, "include")
        )

    def _set_cmake_defs(self, defs):
        defs["OXYGEN_BUILD_TOOLS"] = bool(self.options.tools)
        defs["OXYGEN_BUILD_EXAMPLES"] = bool(self.options.examples)
        defs["OXYGEN_BUILD_TESTS"] = bool(self.options.tests)
        defs["OXYGEN_BUILD_BENCHMARKS"] = bool(self.options.benchmarks)
        defs["OXYGEN_BUILD_DOCS"] = bool(self.options.docs)
        defs["BUILD_SHARED_LIBS"] = bool(self.options.shared)

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
