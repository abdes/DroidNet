# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import os
import json
import re
import subprocess
import shutil
import hashlib
from typing import Any
from conan import ConanFile  # type: ignore
from conan.tools.cmake import CMakeToolchain, CMakeConfigDeps  # type: ignore
from conan.tools.files import load, copy, save  # type: ignore
from conan.tools.cmake import cmake_layout, CMake  # type: ignore
from conan.tools.microsoft import is_msvc_static_runtime, is_msvc  # type: ignore
from conan.errors import ConanInvalidConfiguration  # type: ignore
from conan.tools.build import check_min_cppstd, cross_building  # type: ignore
from conan.tools.scm import Version  # type: ignore
from conan.tools.env import VirtualRunEnv  # type: ignore
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
    name = "oxygen"
    package_type = "library"

    # Metadata
    description = "Oxygen Game Engine."
    license = "BSD 3-Clause License"
    homepage = "https://github.com/abdes/oxygen"
    url = "https://github.com/abdes/oxygen/"
    topics = ("graphics programming", "gamedev", "math")

    # Binary model: Settings and Options
    # Ordinary consumers need only Conan's standard settings. Sanitizer profiles
    # retain their dependency identity through tools.info.package_id:confs.
    settings = "os", "arch", "compiler", "build_type"
    options: Any = {
        # Options
        "shared": [True, False],
        "fPIC": [True, False],
        "awaitable_state_checker": [True, False, "auto"],
        "with_asan": [True, False],
        "with_tracy": [True, False],
        # Optional components:
        "modules": ["ANY"],
        # Optional development outputs (core cooker tools and RenderScene are mandatory):
        "tools": [True, False],
        "examples": [True, False],
        "tests": [True, False],
        "benchmarks": [True, False],
        "docs": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        "awaitable_state_checker": "auto",
        "with_asan": False,
        "with_tracy": False,
        # Optional components:
        "modules": "full",
        # Optional development outputs:
        "tools": False,
        "examples": False,
        "tests": False,
        "benchmarks": False,
        "docs": False,
        # Dependencies options:
        "fmt/*:header_only": True,
        "sdl/*:shared": True,
    }

    exports = ("VERSION",)
    exports_sources = (
        "VERSION",
        "AUTHORS",
        "README.md",
        "LICENSE",
        "CMakeLists.txt",
        "CMakePresets.json",
        ".clangd",
        ".clang-format",
        "doxygen/**",
        "cmake/**",
        "src/**",
        "Examples/**",
        "tools/**",
        "!out/**",
        "!build/**",
        "!cmake-build-*/**",
        "!src/Oxygen/Core/version-info.h",
        "!**/__pycache__/**",
        "!**/*.egg-info/**",
        "!**/.pytest_cache/**",
        "!**/.venv/**",
        "!**/.cooked/**",
        "!**/.cooked.stage-*/**",
        "!**/.cooked.backup-*/**",
        "!Examples/Content/pak/**",
        "!Examples/RenderScene/pak/**",
        "!Examples/RenderScene/demo_settings.json",
        "!Examples/RenderScene/.reimport-runs/**",
        "!Examples/RenderScene/reimport-sources.local.json",
    )

    @staticmethod
    def _module_closure(selection):
        names = ("Base", "Composition", "OxCo", "Serio", "TextWrap", "Clap")
        if str(selection) == "full":
            return ()
        requested = {name.strip() for name in str(selection).split(",")}
        invalid = requested.difference(names)
        if invalid:
            raise ConanInvalidConfiguration(
                f"Invalid modules selection: {sorted(invalid)}. Use 'full' or "
                f"comma-separated module names: {', '.join(names)}.")
        requested.add("Base")
        if "Clap" in requested:
            requested.add("TextWrap")
        return tuple(name for name in names if name in requested)

    @property
    def _modules(self):
        return self._module_closure(self.options.modules)

    @property
    def _full_engine(self):
        return not self._modules

    @staticmethod
    def _checker_enabled(value, build_type):
        return str(build_type) == "Debug" if str(value) == "auto" else str(value) == "True"

    def package_id(self):
        modules = self._module_closure(self.info.options.modules)
        self.info.options.modules = ",".join(modules) if modules else "full"
        self.info.options.awaitable_state_checker = self._checker_enabled(
            self.info.options.awaitable_state_checker, self.info.settings.build_type)

    def set_version(self):
        assert (
            self.recipe_folder is not None
        ), "recipe_folder must be set before set_version()"
        version = self._read_product_version()
        if self.version is not None and str(self.version) != version:
            raise ConanInvalidConfiguration("The package version must match Oxygen's VERSION file.")
        self.version = version

    def _read_product_version(self):
        version = load(self, Path(self.recipe_folder) / "VERSION").strip()
        if (not re.fullmatch(r"(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})", version)
                or any(int(part) > 255 for part in version.split("."))):
            raise ConanInvalidConfiguration(
                f"Invalid version '{version}'; expected major.minor.patch, "
                "with canonical decimal components in 0..255."
            )
        return version

    def _source_provenance(self):
        """Same schema and Git scope as cmake/SourceProvenance.cmake; no build tools required."""
        root = Path(self.recipe_folder)
        version = self._read_product_version()
        capsule = root / "oxygen-source.json"
        if capsule.is_file():
            data = json.loads(capsule.read_text(encoding="utf-8"))
            valid = (isinstance(data, dict) and set(data) == {"schema", "version", "commit", "dirty"}
                     and type(data["schema"]) in (int, float) and data["schema"] == 1 and data["version"] == version)
            if valid:
                commit, dirty = data["commit"], data["dirty"]
                valid = ((commit is None and dirty is None)
                         or (isinstance(commit, str) and re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", commit)
                             and type(dirty) is bool))
            if not valid:
                raise ConanInvalidConfiguration(f"Invalid or mismatched source provenance: {capsule}")
            return {"schema": 1, "version": version, "commit": commit, "dirty": dirty}

        unknown = {"schema": 1, "version": version, "commit": None, "dirty": None}

        def git(*arguments):
            result = subprocess.run(
                ["git", "--no-optional-locks", "-C", str(root), *arguments],
                capture_output=True, text=True, encoding="utf-8", errors="replace", check=True,
            )
            return result.stdout.strip()

        try:
            git("ls-files", "--error-unmatch", "--", "VERSION", "CMakeLists.txt")
            if git("rev-parse", "--is-shallow-repository") != "false":
                return unknown
            # No shared repository build files outside this directory are consumed.
            scope = [".", ":(exclude)plans/CMAKE_CONAN_MODERNIZATION_PLAN.md"]
            commit = git("log", "-1", "--format=%H", "--", *scope)
            if not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", commit):
                return unknown
            dirty = bool(git("status", "--porcelain=v1", "--untracked-files=all", "--", *scope))
        except (OSError, subprocess.CalledProcessError):
            return unknown
        return {"schema": 1, "version": version, "commit": commit, "dirty": dirty}

    def export(self):
        # Export freezes the source identity; local install/generate never does.
        save(self, Path(self.export_folder) / "oxygen-source.json",
             json.dumps(self._source_provenance(), indent=2) + "\n")

    def export_sources(self):
        copy(self, "oxygen-source.json", src=self.export_folder, dst=self.export_sources_folder)
        workspace = self._python_workspace(Path(self.recipe_folder))
        if workspace:
            copy(self, ".python-version", src=workspace, dst=self.export_sources_folder)
            # Freeze only Oxygen's Python build-tool closure into the source
            # export. Cache builds must not reach back into the monorepo.
            destination = Path(self.export_sources_folder) / "cmake" / "python"
            common = ["uv", "export", "--project", str(workspace), "--locked",
                      "--no-python-downloads", "--no-default-groups", "--no-emit-workspace",
                      "--no-header", "--no-annotate"]
            tools = ["--package", "bindless-codegen", "--package", "oxygen-pakgen", "--extra", "yaml"]
            for name, selection in (
                ("build-backends.txt", ["--only-group", "build-tools"]),
                ("build-requirements.txt", tools),
                ("test-requirements.txt", tools + ["--extra", "test"]),
            ):
                result = subprocess.run(common + selection, capture_output=True, text=True, encoding="utf-8")
                if result.returncode:
                    raise ConanInvalidConfiguration(f"Cannot export Python lock: {result.stderr.strip()}")
                save(self, str(destination / name), result.stdout)

    @staticmethod
    def _python_workspace(start):
        for directory in (start, *start.parents):
            manifest = directory / "pyproject.toml"
            if (directory / "uv.lock").is_file() and manifest.is_file():
                if "[tool.uv.workspace]" in manifest.read_text(encoding="utf-8"):
                    return directory
        return None

    def _prepare_build_python(self):
        source = Path(self.source_folder or self.recipe_folder)
        if not (source / "CMakeLists.txt").is_file():
            return None  # Dependency/deployment-only use of the recipe.
        workspace = self._python_workspace(source)
        uv = shutil.which("uv")
        if (workspace or self._full_engine) and not uv:
            raise ConanInvalidConfiguration("uv is required to provision Oxygen's Python build tools.")
        if workspace:
            environment = workspace / ".venv"
            self.output.info("Synchronizing the repository Python environment from uv.lock")
            subprocess.run([uv, "sync", "--project", str(workspace), "--locked",
                            "--no-python-downloads", "--no-active"], check=True,
                           env={**os.environ, "UV_PROJECT_ENVIRONMENT": str(environment)})
            lockfile = workspace / "uv.lock"
        elif self._full_engine:
            # One private environment for this independently exported source
            # build, not one per configuration in a developer checkout.
            environment = Path(self.build_folder) / ".venv"
            interpreter = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
            name = "test-requirements.txt" if self.options.tests else "build-requirements.txt"
            lockfile = source / "cmake" / "python" / name
            if not lockfile.is_file():
                raise ConanInvalidConfiguration(f"Missing exported Python requirements: {lockfile}")
            if not interpreter.is_file():
                version_file = source / ".python-version"
                if not version_file.is_file():
                    raise ConanInvalidConfiguration(f"Missing exported Python version request: {version_file}")
                subprocess.run([uv, "venv", "--python", version_file.read_text(encoding="utf-8").strip(), "--no-python-downloads",
                                str(environment)], check=True)
            backends = lockfile.with_name("build-backends.txt")
            subprocess.run([uv, "pip", "sync", "--python", str(interpreter), "--require-hashes",
                            str(lockfile), str(backends)], check=True)
            subprocess.run([uv, "pip", "install", "--python", str(interpreter),
                            "--no-deps", "--no-build-isolation", "--editable",
                            str(source / "src/Oxygen/Core/Tools/BindlessCodeGen"), "--editable",
                            str(source / "src/Oxygen/Cooker/Tools/PakGen")], check=True)
            save(self, str(environment / "oxygen-build-tools.json"), json.dumps({
                "requirements_sha256": hashlib.sha256(lockfile.read_bytes() + backends.read_bytes()).hexdigest(),
                "pyprojects": {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in (source / "src/Oxygen/Core/Tools/BindlessCodeGen/pyproject.toml",
                                         source / "src/Oxygen/Cooker/Tools/PakGen/pyproject.toml")},
            }, indent=2) + "\n")
        else:
            return None
        interpreter = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        return interpreter.as_posix(), lockfile.as_posix()

    def requirements(self):
        self.requires("fmt/12.1.0", transitive_headers=True)
        if self._full_engine or "OxCo" in self._modules:
            self.requires("asio/1.36.0", transitive_headers=True)
        if self._full_engine or "Clap" in self._modules:
            self.requires("magic_enum/0.9.7", transitive_headers=True)

        self._test_deps = set()
        if self.options.tests:
            self.test_requires("gtest/master")
            self._test_deps.add("gtest")
        if self.options.benchmarks and (self._full_engine or {"OxCo", "Composition"}.intersection(self._modules)):
            self.test_requires("benchmark/1.9.4")
            self._test_deps.add("benchmark")
        if not self._full_engine:
            return

        # ShaderBake links the host API; shader probes execute a build-machine tool.
        self.requires("dxc/1.9.2607")
        self.requires("sdl/3.2.28", transitive_headers=True, transitive_libs=True)
        self.requires("imgui/1.92.5", transitive_headers=True, transitive_libs=True)
        self.requires("glm/1.0.1", transitive_headers=True)
        self.requires("nlohmann_json/3.11.3", transitive_headers=True)
        self.requires("json-schema-validator/2.4.0")
        self.requires("tinyexr/1.0.12")
        self.requires("pdcurses/3.9")
        self.requires("ftxui/6.1.9")
        self.requires("libspng/0.7.4")
        self.requires("luau/0.739", transitive_headers=True, transitive_libs=True)
        self.requires("joltphysics/5.5.0", transitive_headers=True, transitive_libs=True)
        self.requires("xxhash/0.8.3")
        self.requires("tracy/0.13.1")

    def build_requirements(self):
        if self._full_engine:
            self.tool_requires("dxc/1.9.2607")

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
            # When building shared libs, and compiler is MSVC, we need to set
            # the runtime to dynamic
            if is_msvc(self) and is_msvc_static_runtime(self):
                raise ConanInvalidConfiguration("Shared Oxygen libraries require the dynamic MSVC runtime.")

        if self.options.tests:
            self.options["gtest"].shared = False
        if self._full_engine:
            # Explicit options for the pinned recipes. Missing/renamed options
            # must fail during graph construction rather than silently diverge.
            self.options["tinyexr"].with_thread = True
            self.options["tinyexr"].with_openmp = not self._with_asan
            self.options["pdcurses"].enable_widec = True
            self.options["tracy"].enable = bool(self.options.with_tracy)
            self.options["tracy"].shared = bool(self.options.shared)

    def validate(self):
        if self._full_engine and (self.settings.os != "Windows" or self.settings.arch != "x86_64"):
            raise ConanInvalidConfiguration(
                "Oxygen's full-engine build requires Windows x64."
            )
        if self._full_engine and (
            self.settings.compiler != "msvc"
            or Version(str(self.settings.compiler.version)) < "195"
        ):
            raise ConanInvalidConfiguration(
                "Oxygen requires MSVC 19.50 or newer "
                "(Conan compiler=msvc, compiler.version>=195)."
            )
        if self._full_engine and cross_building(self):
            raise ConanInvalidConfiguration("Full-engine builds require a native Windows x64 build machine.")
        check_min_cppstd(self, 23)

        identity = self.conf.get("user.oxygen:sanitizer", default="none")
        if not self._with_asan and identity != "none":
            raise ConanInvalidConfiguration("The ASan dependency profile requires oxygen/*:with_asan=True.")

        if self._with_asan:
            identity_confs = self.conf.get("tools.info.package_id:confs", default=[], check_type=list)
            cflags = self.conf.get("tools.build:cflags", default=[], check_type=list)
            cxxflags = self.conf.get("tools.build:cxxflags", default=[], check_type=list)
            if (identity != "asan" or "user.oxygen:sanitizer" not in identity_confs
                    or "-fsanitize=address" not in cflags or "-fsanitize=address" not in cxxflags):
                raise ConanInvalidConfiguration(
                    "ASan requires the ASan host profile, including its dependency "
                    "binary-identity configuration; with_asan alone is insufficient."
                )

        if self._with_asan and self.settings.build_type != "Debug":
            raise ConanInvalidConfiguration(
                "ASan is only supported for Debug builds. "
                f"Current build_type is {self.settings.build_type}."
            )

    def _require_package_linkage(self):
        if self._full_engine and not self.options.shared:
            raise ConanInvalidConfiguration(
                "Full-engine Conan packages require shared=True: RenderScene loads "
                "its graphics backends as DLLs. Static packages require an explicit "
                "reusable-module selection.")

    @property
    def _cmake_generator(self):
        """Use Conan's resolved generator, independently of the calling IDE."""
        return CMakeToolchain(self).generator

    @property
    def _install_root(self):
        return Path(self.recipe_folder) / "out" / (
            "install-tracy" if self.options.with_tracy else "install")

    @property
    def _with_asan(self):
        return bool(self.options.with_asan)

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
        generator = self._cmake_generator
        if generator == "Ninja Multi-Config":
            parts.append("ninja")
        elif generator.startswith("Visual Studio"):
            parts.append("vs")
        elif generator == "Ninja":
            parts.append("ninja-single")
        else:
            parts.append(re.sub(r"[^a-z0-9]+", "-", generator.lower()).strip("-"))
        return "-".join(parts)

    def _check_build_identity(self):
        """Reject incompatible reuse before replacing generated build metadata."""
        settings = dict(self.settings.items())
        # These vary normally between Debug and Release in one multi-config tree.
        settings.pop("build_type", None)
        settings.pop("compiler.runtime_type", None)
        identity = {"generator": self._cmake_generator, "settings": settings,
                    "shared": bool(self.options.shared),
                    "asan": self._with_asan, "tracy": bool(self.options.with_tracy),
                    "fPIC": str(self.options.get_safe("fPIC"))}
        # These become one set of configure-time choices, even when dependency
        # binaries are installed separately for each build configuration.
        choices = {name: str(self.options.get_safe(name)) for name in (
            "awaitable_state_checker", "tools", "examples", "tests", "benchmarks", "docs")}
        choices["modules"] = ";".join(self._modules)
        configurations = {}
        configuration = str(self.settings.build_type)
        path = Path(self.generators_folder) / "oxygen-build-identity.json"
        remedy = (
            f"Regenerate the selected build tree '{self.build_folder}' consistently "
            "(build-tree.ps1 generate <profile> -Clean for the selected family/generator, "
            "or remove that build tree and "
            "repeat its Conan installs). Other build trees need not be removed.")
        if path.exists():
            previous = json.loads(path.read_text(encoding="utf-8"))
            differences = [f"{key}: {previous.get(key)!r} -> {value!r}"
                           for key, value in identity.items() if previous.get(key) != value]
            if differences:
                raise ConanInvalidConfiguration(
                    "Incompatible Oxygen build-tree reuse: " + "; ".join(differences)
                    + ". " + remedy)
            configurations = previous.get("configurations", {})
            for other, other_choices in configurations.items():
                if other == configuration:
                    continue
                changed = [name for name, value in choices.items()
                           if other_choices.get(name) != value]
                if changed:
                    raise ConanInvalidConfiguration(
                        f"Incompatible Oxygen build-tree options for {configuration} and {other}: "
                        + ", ".join(changed) + ". " + remedy)
        elif (path.parent / "conan_toolchain.cmake").exists():
            raise ConanInvalidConfiguration(
                "This existing Oxygen tree predates build-identity validation; "
                "its compatibility cannot be verified. " + remedy)
        configurations[configuration] = choices
        identity["configurations"] = configurations
        return path, identity

    def generate(self):
        # Package destinations exist for cache builds, not for local dependency
        # installation. Do not reject a contributor's static development graph.
        if self.package_folder:
            self._require_package_linkage()
        identity_path, identity = self._check_build_identity()
        python = self._prepare_build_python()
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
        tc.cache_variables["OXYGEN_WITH_TRACY"] = bool(self.options.with_tracy)
        expectations = dict(tc.cache_variables)
        package_build = bool(self.package_folder)
        modules = ";".join(self._modules)
        checker = {"auto": "AUTO", "True": "ON", "False": "OFF"}[str(self.options.awaitable_state_checker)]
        tc.cache_variables["OXYGEN_MODULES"] = modules
        tc.cache_variables["OXYGEN_AWAITER_STATE_CHECKER"] = checker
        tc.cache_variables["CMAKE_INTERMEDIATE_DIR_STRATEGY"] = "SHORT"
        if python:
            tc.cache_variables["Python3_EXECUTABLE"] = python[0]
            tc.variables["OXYGEN_PYTHON_LOCKFILE"] = python[1]
        dxc_tool_dirs = dxc_runtime_dirs = ""
        if "dxc" in self.dependencies.host:
            dxc_tool_dirs = ";".join(Path(p).as_posix() for p in self.dependencies.build["dxc"].cpp_info.bindirs)
            dxc_runtime_dirs = ";".join(Path(p).as_posix() for p in self.dependencies.host["dxc"].cpp_info.bindirs)

        class OxygenOptionsBlock:
            # Graph facts must not be cached: every configure reads the current
            # install's expectations, even after recipe options have changed.
            template = """
{% for name, value in options.items() %}
set(OXYGEN_CONAN_EXPECT_{{ name }} {{ 'ON' if value else 'OFF' }})
{% endfor %}
set(OXYGEN_CONAN_PACKAGE_BUILD {{ 'ON' if package_build else 'OFF' }})
set(OXYGEN_CONAN_MODULES [==[{{ modules }}]==])
set(OXYGEN_CONAN_AWAITER_STATE_CHECKER {{ checker }})
set(OXYGEN_DXC_TOOL_BINDIRS [==[{{ dxc_tool_dirs }}]==])
set(OXYGEN_DXC_RUNTIME_BINDIRS [==[{{ dxc_runtime_dirs }}]==])
"""

            def context(self):
                return {"options": expectations, "package_build": package_build,
                        "dxc_tool_dirs": dxc_tool_dirs, "dxc_runtime_dirs": dxc_runtime_dirs,
                        "modules": modules, "checker": checker}

        tc.blocks["oxygen_options"] = OxygenOptionsBlock
        # Set OXYGEN_CONAN_DEPLOY_DIR to the base install directory.
        # The default local install rules select Debug, Release or Asan.
        # Explicit prefixes and Conan package roots remain unchanged.
        if not package_build:
            tc.variables["OXYGEN_CONAN_DEPLOY_DIR"] = self._install_root.as_posix()
            tc.variables["OXYGEN_SDK_DEPENDENCY_DIR"] = (
                Path(self.generators_folder) / "oxygen-sdk").as_posix()

        self._reset_legacy_presets(tc.presets_prefix)
        tc.generate()

        deps = CMakeConfigDeps(self)
        deps.generate()
        if not package_build:
            self._generate_sdk_dependencies()
            self._generate_project_presets()
        save(self, str(identity_path), json.dumps(identity, indent=2) + "\n")

    @staticmethod
    def _sdk_runtime_file(path):
        """Runtime DLLs and their matching development symbols, not build tools."""
        return path.suffix.lower() == ".dll" or (
            path.suffix.lower() == ".pdb" and path.with_suffix(".dll").is_file())

    def _sdk_payload(self):
        """Select public headers, declared libraries and runtime payloads only."""
        packages = {}
        directories = []
        libraries = {}
        for require, dep in self.dependencies.host.items():
            if require.test or dep.ref.name in getattr(self, "_test_deps", set()):
                continue
            name = dep.ref.name
            packages[name] = dep
            folder = Path(dep.package_folder)
            info = dep.cpp_info.aggregated_components()
            for kind in ("includedirs", "bindirs", "resdirs"):
                for raw in getattr(info, kind, []) or []:
                    source = Path(raw)
                    if not source.is_absolute():
                        source = folder / source
                    if not source.is_dir():
                        continue
                    try:
                        relative = source.relative_to(folder)
                    except ValueError:
                        continue  # Platform SDK paths remain platform-owned.
                    if relative == Path('.'):
                        raise ConanInvalidConfiguration(f"SDK needs an explicit {kind} for {name}")
                    directories.append((source, relative, kind == "bindirs"))
            if (folder / "licenses").is_dir():
                directories.append((folder / "licenses", Path("share/oxygen/licenses") / name, False))
            for lib in info.libs:
                candidates = set()
                for raw in info.libdirs:
                    directory = Path(raw)
                    if not directory.is_absolute():
                        directory = folder / directory
                    if not directory.is_dir():
                        continue
                    # cpp_info.libs supplies logical link names, not every
                    # archive that happens to have been packaged upstream.
                    stems = {str(lib).casefold(), ("lib" + str(lib)).casefold()}
                    for item in directory.rglob('*'):
                        if item.is_file() and (item.stem.casefold() in stems
                                               and item.suffix.lower() in ('.lib', '.a', '.so', '.dylib')):
                            candidates.add(item)
                if not candidates:
                    raise ConanInvalidConfiguration(f"SDK cannot locate declared library {name}:{lib}")
                for item in sorted(candidates):
                    previous = libraries.get(item.name.casefold())
                    if previous and previous != item and previous.read_bytes() != item.read_bytes():
                        raise ConanInvalidConfiguration(f"Declared SDK library collision: {previous} and {item}")
                    libraries[item.name.casefold()] = item
        # Validate the flattened payload before deploy() can overwrite anything.
        destinations = {}
        for source, relative, runtime in directories:
            for artifact in source.rglob("*"):
                if not artifact.is_file() or (runtime and not self._sdk_runtime_file(artifact)):
                    continue
                destination = (relative / artifact.relative_to(source)).as_posix().casefold()
                existing = destinations.get(destination)
                if existing and existing != artifact and existing.read_bytes() != artifact.read_bytes():
                    raise ConanInvalidConfiguration(f"SDK file collision: {destination}")
                destinations[destination] = artifact
        return packages, directories, list(libraries.values())

    def _generate_sdk_dependencies(self):
        """Install declared artifacts and rebase Conan's generated target metadata."""
        config = str(self.settings.build_type)
        packages, directories, libraries = self._sdk_payload()
        identity = "\n".join(f"{name}:{dep.ref}:{dep.package_folder}" for name, dep in sorted(packages.items()))
        root = Path(self.generators_folder) / "oxygen-sdk"
        output = root / config / hashlib.sha256(identity.encode()).hexdigest()[:16]
        output.mkdir(parents=True, exist_ok=True)

        def quote(value):
            return '[==[' + str(value).replace('\\', '/') + ']==]'

        rules = ["# Generated from Oxygen's resolved Conan host graph."]
        for source, relative, runtime in directories:
            component = "Oxygen_runtime" if runtime else "Oxygen_dev"
            if not runtime:
                rules.append(f'install(DIRECTORY {quote(source.as_posix() + "/")} DESTINATION {quote(relative)} COMPONENT {component} CONFIGURATIONS {config})')
            for artifact in source.rglob('*'):
                if not artifact.is_file() or (runtime and not self._sdk_runtime_file(artifact)):
                    continue
                if runtime:
                    destination_dir = relative / artifact.parent.relative_to(source)
                    artifact_component = "Oxygen_dev" if artifact.suffix.lower() == ".pdb" else "Oxygen_runtime"
                    rules.append(f'install(FILES {quote(artifact)} DESTINATION {quote(destination_dir)} COMPONENT {artifact_component} CONFIGURATIONS {config})')
        for library in libraries:
            rules.append(f'install(FILES {quote(library)} DESTINATION "${{OXYGEN_INSTALL_LIB}}" COMPONENT Oxygen_dev CONFIGURATIONS {config})')

        names = [dep.cpp_info.get_property("cmake_file_name") or name for name, dep in sorted(packages.items())]
        roots = [Path(dep.package_folder) for dep in packages.values()]
        metadata = {name: {"files": [], "targets": set(), "requires": set()} for name in names}
        common_files = []
        for source in Path(self.generators_folder).glob('*.cmake'):
            lower = source.name.lower()
            if lower != 'cmakedeps_macros.cmake' and not any(
                    lower.startswith(n.lower() + suffix) for n in names
                    for suffix in ('-', 'config', 'targets')):
                continue
            suffix_config = re.search(r'-(debug|release|relwithdebinfo|minsizerel)(?:-|\.)', lower)
            if suffix_config and suffix_config.group(1) != config.lower():
                continue
            text = source.read_text(encoding='utf-8')
            for library in libraries:
                target_path = '${_OXYGEN_SDK_LIBRARY_DIR}/' + library.name
                text = text.replace(library.as_posix(), target_path)
                for folder in roots:
                    if library.is_relative_to(folder):
                        relative = library.relative_to(folder).as_posix()
                        text = re.sub(r'\$\{[^}]*PACKAGE_FOLDER[^}]*\}/' + re.escape(relative),
                                      lambda _: target_path, text)
            for folder in sorted(roots, key=lambda item: len(str(item)), reverse=True):
                text = text.replace(folder.as_posix(), '${_OXYGEN_SDK_PREFIX}')
            # CMakeDeps resolves logical libraries through these variables;
            # CMakeConfigDeps instead supplies explicit imported locations above.
            text = re.sub(
                r'(set\(\s*[^\s()]+_LIB_DIRS_[A-Z0-9_]+)\s+([^\n)]*)\)',
                lambda match: match.group(1) + ' "${_OXYGEN_SDK_LIBRARY_DIR}")'
                if 'PACKAGE_FOLDER' in match.group(2) or '_OXYGEN_SDK_PREFIX' in match.group(2)
                else match.group(0), text)
            text = text.replace('${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake',
                                '${CMAKE_CURRENT_LIST_DIR}/../Oxygen/cmakedeps_macros.cmake')
            text = ('include("${CMAKE_CURRENT_LIST_DIR}/../Oxygen/OxygenSDKPaths.cmake")\n'
                    + text)
            target = output / source.name
            save(self, target, text)
            owner = next((name for name in names if any(
                lower.startswith(name.lower() + suffix) for suffix in ('-', 'config', 'targets'))), None)
            if owner:
                metadata[owner]["files"].append(target)
                metadata[owner]["targets"].update(re.findall(r'add_library\(\s*([^\s)]+)', text))
                metadata[owner]["requires"].update(re.findall(r'find_dependency\(\s*([A-Za-z0-9_.-]+)', text))
                # CMakeDeps declares component targets and dependency package
                # names in data variables instead of literal add/find calls.
                for values in re.findall(
                        r'(?:set\(\s*\S+_COMPONENT_NAMES|list\(\s*APPEND\s+\S+_COMPONENT_NAMES)\s+([^)]*)\)', text):
                    metadata[owner]["targets"].update(
                        token.strip('"') for token in values.split()
                        if '::' in token and '$' not in token)
                for values in re.findall(
                        r'(?:set\(\s*\S+_FIND_DEPENDENCY_NAMES|list\(\s*APPEND\s+\S+_FIND_DEPENDENCY_NAMES)\s+([^)]*)\)', text):
                    metadata[owner]["requires"].update(token.strip('"') for token in values.split())
            else:
                common_files.append(target)
        # Only metadata referenced by the exported Oxygen interfaces belongs to
        # the SDK. Selection happens while defining install rules, not by copying
        # every package and deleting files afterwards.
        rules += ['set(_sdk_needed)', 'set(_sdk_links "")',
                  'get_property(_sdk_targets GLOBAL PROPERTY OXYGEN_INSTALLED_TARGETS)',
                  'foreach(_target IN LISTS _sdk_targets)',
                  '  get_target_property(_links "${_target}" INTERFACE_LINK_LIBRARIES)',
                  '  string(APPEND _sdk_links ";${_links}")', 'endforeach()']
        for name, entry in metadata.items():
            for target in sorted(target for target in entry["targets"] if "$" not in target):
                rules += [f'string(FIND "${{_sdk_links}}" {quote(target)} _position)',
                          f'if(NOT _position EQUAL -1)\n  list(APPEND _sdk_needed {quote(name)})\nendif()']
        rules += ['list(REMOVE_DUPLICATES _sdk_needed)', 'set(_sdk_changed TRUE)', 'while(_sdk_changed)',
                  '  set(_sdk_before "${_sdk_needed}")']
        for name, entry in metadata.items():
            children = sorted(entry["requires"].intersection(metadata))
            if children:
                rules += [f'  if({quote(name)} IN_LIST _sdk_needed)',
                          '    list(APPEND _sdk_needed ' + ' '.join(quote(c) for c in children) + ')', '  endif()']
        rules += ['  list(REMOVE_DUPLICATES _sdk_needed)',
                  '  if(_sdk_before STREQUAL _sdk_needed)\n    set(_sdk_changed FALSE)\n  endif()', 'endwhile()',
                  'set(_sdk_registry "include(CMakeFindDependencyMacro)\\n")']
        for name, entry in metadata.items():
            # Explicit local package dirs prevent fallback to a producer cache.
            statement = f'set({name}_DIR "${{CMAKE_CURRENT_LIST_DIR}}/../{name}")\n'
            rules += [f'if({quote(name)} IN_LIST _sdk_needed)',
                      '  string(APPEND _sdk_registry ' + quote(statement) + ')', 'endif()']
        for name, entry in metadata.items():
            rules += [f'if({quote(name)} IN_LIST _sdk_needed)',
                      '  install(FILES ' + ' '.join(quote(p) for p in entry["files"])
                      + f' DESTINATION "${{OXYGEN_INSTALL_LIB}}/cmake/{name}" COMPONENT Oxygen_dev CONFIGURATIONS {config})',
                      '  string(APPEND _sdk_registry ' + quote(f'find_dependency({name} CONFIG REQUIRED)\n') + ')', 'endif()']
        selected = '${PROJECT_BINARY_DIR}/sdk-metadata/' + config + '/OxygenDependencies.cmake'
        rules += [f'file(CONFIGURE OUTPUT "{selected}" CONTENT "${{_sdk_registry}}" @ONLY)',
                  f'install(FILES "{selected}" ' + ' '.join(quote(p) for p in common_files)
                  + f' DESTINATION "${{OXYGEN_INSTALL_CMAKE}}" COMPONENT Oxygen_dev CONFIGURATIONS {config})',
                  f'message(STATUS "Oxygen SDK dependency metadata ({config}): ${{_sdk_needed}}")']
        save(self, root / f'install-{config}.cmake', '\n'.join(rules) + '\n')

    def _generate_project_presets(self):
        """Derive project presets only for installed trees and configurations.

        CMakeUserPresets implicitly includes CMakePresets, so ordinary CMake
        inheritance joins the root policy and native Conan metadata without
        editing either. Missing trees never need placeholder presets.
        """
        if not self.source_folder or self.source_folder == self.generators_folder:
            return
        # Only source-workspace build trees belong in its IDE preset collection.
        # An export-pkg output folder may be elsewhere even when package_folder
        # is not set during generate(). Leave its native presets standalone.
        workspace_output = (Path(self.source_folder) / "out").resolve()
        current = (Path(self.generators_folder) / "CMakePresets.json").resolve()
        if not current.is_relative_to(workspace_output):
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
        includes = {current}
        includes.update(candidate for p in previous.get("include", [])
                        if (candidate := (path.parent / p).resolve()).is_relative_to(workspace_output))
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
        for section in ("configurePresets", "buildPresets", "testPresets"):
            names = set()
            for preset in data[section]:
                if preset["name"] in names:
                    raise ConanInvalidConfiguration(
                        f"Duplicate {section} name '{preset['name']}'; "
                        f"leaving {path} unchanged.")
                names.add(preset["name"])
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
        cmake_layout(self, generator=self._cmake_generator,
                     build_folder=f"out/build-{self._build_variant}")

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
        # Native build steps execute ShaderBake and the cooker tools. Their host
        # DLLs come from this graph even when no developer SDK was deployed.
        with VirtualRunEnv(self).vars().apply():
            cmake.build()
            if self.options.tests:
                cmake.test()

    def package(self):
        self._require_package_linkage()
        cmake = CMake(self)
        cmake.install()
        self._package_component_metadata()
        copy(self, "oxygen-source.json", src=self.source_folder,
             dst=str(Path(self.package_folder) / "share" / "oxygen"))

    def _package_component_metadata(self):
        """Read the built targets, not a second handwritten engine dependency graph."""
        build = Path(self.build_folder)
        metadata = build / "conan-metadata"
        names = json.loads((metadata / "targets.json").read_text(encoding="utf-8"))
        reply = build / ".cmake/api/v1/reply"
        index = json.loads(max(reply.glob("index-*.json")).read_text(encoding="utf-8"))
        model_file = next(item["jsonFile"] for item in index["objects"] if item["kind"] == "codemodel")
        model = json.loads((reply / model_file).read_text(encoding="utf-8"))
        if model["version"]["minor"] < 9:
            raise ConanInvalidConfiguration("Component metadata requires CMake's codemodel 2.9 or newer.")
        config = next(item for item in model["configurations"] if item["name"] == str(self.settings.build_type))
        entries = config["targets"] + config.get("abstractTargets", [])
        targets = {item["id"]: json.loads((reply / item["jsonFile"]).read_text(encoding="utf-8"))
                   for item in entries}
        by_name = {item["name"]: item for item in targets.values() if item["name"] in names}

        # CMakeConfigDeps may consume upstream native configs. Map their target
        # definitions back to the owning Conan package when no explicit target
        # name property exists (SDL is one such package).
        external_targets = {}
        package_roots = {}
        package_names = {}
        for requirement, dependency in self.dependencies.host.items():
            if requirement.test:
                continue
            package = dependency.ref.name
            package_roots[package] = Path(dependency.package_folder).resolve()
            package_names[package] = dependency.cpp_info.get_property("cmake_file_name") or package
            infos = [(package, dependency.cpp_info), *dependency.cpp_info.components.items()]
            for component, info in infos:
                target = info.get_property("cmake_target_name") or f"{package}::{component}"
                external_targets[target] = f"{package}::{component}"
                for alias in info.get_property("cmake_target_aliases") or []:
                    external_targets[alias] = f"{package}::{component}"

        def external_requirement(target):
            if target["name"] in external_targets:
                return external_targets[target["name"]]
            files = target.get("backtraceGraph", {}).get("files", [])
            for file in files:
                path = Path(file)
                if not path.is_absolute():
                    continue
                for package, root in package_roots.items():
                    if path.resolve().is_relative_to(root):
                        return f"{package}::{package}"
            raise ConanInvalidConfiguration(f"No Conan owner for exported CMake target {target['name']}.")

        result = {}
        needed_packages = set()
        for target_name, component_name in names.items():
            target = by_name[target_name]
            directory = metadata / str(self.settings.build_type)

            def values(property_name):
                file = directory / f"{target_name}.{property_name}"
                return file.read_text(encoding="utf-8").splitlines() if file.is_file() else []

            component = {
                "libs": values("LIBRARY_NAME"),
                "defines": values("COMPILE_DEFINITIONS-CXX"),
                "cflags": values("COMPILE_OPTIONS-C"),
                "cxxflags": values("COMPILE_OPTIONS-CXX"),
                "sharedlinkflags": values("LINK_OPTIONS"),
                "exelinkflags": values("LINK_OPTIONS"),
                "requires": [], "system_libs": [],
            }
            # Include compile-only edges as well as link-only static edges.
            edges = target.get("interfaceLinkLibraries", []) + target.get("interfaceCompileDependencies", [])
            for edge in edges:
                if "id" in edge:
                    dependency = targets[edge["id"]]
                    if dependency["name"] in names:
                        requirement = names[dependency["name"]]
                    else:
                        requirement = external_requirement(dependency)
                        needed_packages.add(requirement.split("::", 1)[0])
                    if requirement not in component["requires"]:
                        component["requires"].append(requirement)
                else:
                    library = edge["fragment"]
                    if not re.fullmatch(r"[A-Za-z0-9_.+-]+", library):
                        raise ConanInvalidConfiguration(f"Unsupported public library fragment for {target_name}: {library}")
                    if library not in component["system_libs"]:
                        component["system_libs"].append(library)
            result[component_name] = component

        save(self, Path(self.package_folder) / "share/oxygen/conan-components.json",
             json.dumps(result, indent=2) + "\n")
        registry = "include(CMakeFindDependencyMacro)\n" + "".join(
            f"find_dependency({package_names[name]} CONFIG REQUIRED)\n" for name in sorted(needed_packages))
        save(self, Path(self.package_folder) / "lib/cmake/Oxygen/OxygenDependencies.cmake", registry)

    def package_info(self):
        self._require_package_linkage()
        # Native exports preserve CMake's complete usage requirements, including
        # compile features and link-only edges. Other Conan generators receive
        # the same built artifacts, definitions and component relationships.
        self.cpp_info.set_property("cmake_file_name", "Oxygen")
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = ["lib/cmake/Oxygen"]
        metadata = json.loads(load(self, Path(self.package_folder) / "share/oxygen/conan-components.json"))
        public_dependencies = {requirement.split("::", 1)[0]
                               for data in metadata.values() for requirement in data["requires"]
                               if "::" in requirement}
        # Implementation-only and executable-only dependencies remain in the
        # host/runtime graph without becoming consumers' link requirements.
        self.cpp_info.ignored_requires = [dep.ref.name
                                          for requirement, dep in self.dependencies.host.items()
                                          if requirement.direct and not requirement.test
                                          and dep.ref.name not in public_dependencies]
        for name, data in metadata.items():
            component = self.cpp_info.components[name]
            component.set_property("cmake_target_name", f"oxygen::{name}")
            component.includedirs = ["include"]
            component.builddirs = ["lib/cmake/Oxygen"]
            component.resdirs = ["share/oxygen"] if self._full_engine else []
            component.libdirs = ["lib"] if data["libs"] else []
            for field, value in data.items():
                setattr(component, field, value)

    def deploy(self):
        """Deploy Oxygen's dependency interface, not upstream package mirrors."""
        # Conan deploys before generate(): reject incompatible local reuse before
        # copying dependency binaries, not only before writing the toolchain.
        if not self.package_folder:
            self._check_build_identity()
        target = Path(self.deploy_folder) / self._install_subfolder
        _, directories, libraries = self._sdk_payload()
        for source, relative, runtime in directories:
            if runtime:
                for artifact in source.rglob("*"):
                    if artifact.is_file() and self._sdk_runtime_file(artifact):
                        destination = target / relative / artifact.parent.relative_to(source)
                        copy(self, artifact.name, src=artifact.parent, dst=destination)
            else:
                copy(self, "*", src=source, dst=target / relative)
        for library in libraries:
            copy(self, library.name, src=library.parent, dst=target / "lib", keep_path=False)
