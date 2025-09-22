import os
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps
from conan.tools.files import load, copy
from conan.tools.cmake import cmake_layout, CMake
from conan.tools.microsoft import is_msvc_static_runtime, is_msvc
from pathlib import Path
from conan.tools.env import VirtualBuildEnv


class OxygenConan(ConanFile):
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
    options = {
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
        "examples/**",
        "tests/**",
        "benchmarks/**",
        "tools/**",
        "share/**",
        "third_party/**",
        "!out/**",
        "!build/**",
        "!cmake-build-*/**",
    )

    def set_version(self):
        self.version = load(self, Path(self.recipe_folder) / "VERSION").strip()

    def requirements(self):
        self.requires("fmt/11.2.0")
        self.requires("sdl/3.2.20")
        self.requires("imgui/1.92.3")
        self.requires("asio/1.34.2")
        self.requires("glm/1.0.1")
        self.requires("nlohmann_json/3.11.3")
        self.requires("json-schema-validator/2.3.0")
        self.requires("stduuid/1.2.3")
        self.requires("magic_enum/0.9.7")
        # Record test-only dependencies so we can skip them during deploy.
        # The test_requires call accepts a reference like 'gtest/master'.
        self._test_deps = set()
        ref = "gtest/master"
        self.test_requires(ref)
        self._test_deps.add(ref.split("/")[0])

        ref = "benchmark/1.9.1"
        self.test_requires(ref)
        self._test_deps.add(ref.split("/")[0])

        ref = "catch2/3.8.0"
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

    # def configure(self):
    #     if self.options.shared:
    #         self.options.rm_safe("fPIC")
    #     # evaluated options for _check_prereq
    #     options = {k: v[0] == 'T' for k, v in self.options.items()}
    #     # Dependent options
    #     if options['widgets']:
    #         del self.options.text
    #         options['text'] = True
    #     if options['text']:
    #         del self.options.graphics
    #         options['graphics'] = True
    #     if options['script']:
    #         del self.options.data
    #         options['data'] = True
    #     if options['script'] or options['graphics']:
    #         del self.options.vfs
    #         options['vfs'] = True
    #     if not options['tools']:
    #         del self.options.dar_tool
    #         del self.options.dati_tool
    #         del self.options.ff_tool
    #         del self.options.fire_tool
    #         del self.options.shed_tool
    #         del self.options.tc_tool
    #         options['dar_tool'] = False
    #         options['dati_tool'] = False
    #         options['ff_tool'] = False
    #         options['fire_tool'] = False
    #         options['shed_tool'] = False
    #         options['tc_tool'] = False
    #     else:
    #         if not options['widgets']:
    #             del self.options.shed_tool
    #             options['shed_tool'] = False
    #         if not options['script']:
    #             del self.options.fire_tool
    #             options['fire_tool'] = False
    #         if not options['data']:
    #             del self.options.dati_tool
    #             options['dati_tool'] = False
    #         if not options['vfs']:
    #             del self.options.dar_tool
    #             options['dar_tool'] = False
    #         if not options['with_hyperscan']:
    #             del self.options.ff_tool
    #             options['ff_tool'] = False

    #     # Remove system_ options for disabled components
    #     for info in self._requirements():
    #         if not self._check_prereq(info['prereq'], options):
    #             delattr(self.options, info['option'])

    #     # Remove dependent / implicit options
    #     if self.settings.os == "Emscripten" and 'system_zlib' in self.options:
    #         # These are imported from Emscripten Ports
    #         del self.options.system_zlib

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
        self.cpp.source.includedirs.append(generated_headers)

    def _set_cmake_defs(self, defs):
        defs["OXYGEN_BUILD_TOOLS"] = self.options.tools
        defs["OXYGEN_BUILD_EXAMPLES"] = self.options.examples
        defs["OXYGEN_BUILD_TESTS"] = self.options.tests
        defs["OXYGEN_BUILD_BENCHMARKS"] = self.options.tests
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

    def _add_dep(self, opt: str, component, cmake_dep: str, conan_dep=None):
        opt_val = self.options.get_safe(opt)
        if opt_val is None:  # system option deleted
            return
        if opt_val:
            component.system_libs = [cmake_dep]
        else:
            component.requires += [
                conan_dep if conan_dep is not None else cmake_dep
            ]

    def _library_name(self, component: str):
        # Split the string into segments
        segments = component.split("-")
        # Capitalize the first letter of each segment (except the first one)
        lib_name = "Oxygen." + ".".join(word.capitalize() for word in segments)
        self.output.debug("Library for component 'component' is 'lib_name")
        return lib_name

    def package_info(self):
        for name in "OxCo":
            if not self.options.get_safe(name.lower(), True):
                continue  # component is disabled
            component = self.cpp_info.components["oxygen-" + name]
            component.includedirs = ["include"]
            component.builddirs = ["lib/cmake/oxygen"]
            component.libs = []
            component.libdirs = []
            # TODO: need to investigate how to define target with namespace
            component.set_property(
                "cmake_target_name", "oxygen-" + name.lower()
            )
            component.set_property(
                "cmake_target_aliases", ["oxygen::" + name.lower()]
            )

        for name in ["Base"]:
            if not self.options.get_safe(name.lower(), True):
                continue  # component is disabled
            component = self.cpp_info.components["oxygen-" + name]
            component.libs = [self._library_name(name)]
            component.libdirs = ["lib"]

        component = self.cpp_info.components["Base"]
        self._add_dep("system_fmt", component, "fmt::fmt")

        component = self.cpp_info.components["OxCo"]
        component.requires += ["Base"]

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

            # ---- Libraries (flat under lib/) ----
            lib_dir = os.path.join(self.deploy_folder)
            copy(self, "*.lib", src=dep.package_folder, dst=lib_dir)
            copy(self, "*.a", src=dep.package_folder, dst=lib_dir)
            copy(self, "*.so*", src=dep.package_folder, dst=lib_dir)
            copy(self, "*.dylib*", src=dep.package_folder, dst=lib_dir)

            # ---- DLLs / shared libs (flat under bin/) ----
            bin_dir = os.path.join(self.deploy_folder)
            copy(self, "*.dll", src=dep.package_folder, dst=bin_dir)
            copy(self, "*.so*", src=dep.package_folder, dst=bin_dir)
            copy(self, "*.dylib*", src=dep.package_folder, dst=bin_dir)
