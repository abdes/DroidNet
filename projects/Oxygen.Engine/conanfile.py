# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

import os
import json
import re
import subprocess
from typing import Any, cast
from conan import ConanFile  # type: ignore
from conan.tools.cmake import CMakeToolchain, CMakeDeps  # type: ignore
from conan.tools.files import load, copy, save  # type: ignore
from conan.tools.cmake import cmake_layout, CMake  # type: ignore
from conan.tools.microsoft import is_msvc_static_runtime, is_msvc  # type: ignore
from conan.errors import ConanInvalidConfiguration  # type: ignore
from conan.tools.build import check_min_cppstd, cross_building  # type: ignore
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
    # Oxygen consumes the profile's sanitizer setting. The profiles separately
    # extend compiled dependencies' identities through tools.info.package_id:confs;
    # declaring this setting here does not add it to other recipes.
    settings = "os", "arch", "compiler", "build_type", "sanitizer"
    options: Any = {
        # Options
        "shared": [True, False],
        "fPIC": [True, False],
        "awaitable_state_checker": [True, False],
        "with_asan": [True, False],
        "with_tracy": [True, False],
        # Optional components:
        "base": [True, False],
        "oxco": [True, False],
        # Optional development outputs (core cooker tools and RenderScene are mandatory):
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
        "with_tracy": False,
        # Optional components:
        "base": True,
        "oxco": True,
        # Optional development outputs:
        "tools": True,
        "examples": True,
        "tests": True,
        "benchmarks": True,
        "docs": True,
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
        "cmake/**",
        "src/**",
        "Examples/**",
        "tools/**",
        "!out/**",
        "!build/**",
        "!cmake-build-*/**",
        "!src/Oxygen/Core/version-info.h",
    )

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

    def requirements(self):
        # ShaderBake links the host API; shader probes execute a build-machine tool.
        self.requires("dxc/1.9.2607")
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

    def build_requirements(self):
        self.tool_requires("dxc/1.9.2607")

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

        # Preserve threading; MSVC ASan does not support OpenMP.
        try:
            self.options["tinyexr"].with_thread = True
            self.options["tinyexr"].with_openmp = not self._with_asan
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
        if cross_building(self):
            raise ConanInvalidConfiguration("Full-engine builds require a native Windows x64 build machine.")
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

        if self._with_asan:
            identity = self.conf.get("user.oxygen:sanitizer", default="none")
            identity_confs = self.conf.get("tools.info.package_id:confs", default=[], check_type=list)
            if (self.settings.get_safe("sanitizer") != "asan"
                    or identity != "asan" or "user.oxygen:sanitizer" not in identity_confs):
                raise ConanInvalidConfiguration(
                    "ASan requires the ASan host profile, including its dependency "
                    "binary-identity configuration; with_asan alone is insufficient."
                )

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
        tc.cache_variables["OXYGEN_WITH_TRACY"] = bool(self.options.with_tracy)
        expectations = dict(tc.cache_variables)
        package_build = bool(self.package_folder)
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
set(OXYGEN_DXC_TOOL_BINDIRS [==[{{ dxc_tool_dirs }}]==])
set(OXYGEN_DXC_RUNTIME_BINDIRS [==[{{ dxc_runtime_dirs }}]==])
"""

            def context(self):
                return {"options": expectations, "package_build": package_build,
                        "dxc_tool_dirs": dxc_tool_dirs, "dxc_runtime_dirs": dxc_runtime_dirs}

        tc.blocks["oxygen_options"] = OxygenOptionsBlock
        # Set OXYGEN_CONAN_DEPLOY_DIR to the base install directory.
        # The default local install rules select Debug, Release or Asan.
        # Explicit prefixes and Conan package roots remain unchanged.
        install_base = str(
            Path(cast(str, self.recipe_folder)) / "out" / "install"
        )
        tc.variables["OXYGEN_CONAN_DEPLOY_DIR"] = install_base.replace(
            "\\", "/"
        )
        tc.variables["OXYGEN_SDK_DEPENDENCY_DIR"] = (
            Path(self.generators_folder) / "oxygen-sdk"
        ).as_posix()

        self._reset_legacy_presets(tc.presets_prefix)
        tc.generate()
        self._generate_project_presets()

        deps = CMakeDeps(self)
        deps.generate()
        if not package_build:
            self._generate_sdk_dependencies()

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
        import hashlib

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
        copy(self, "oxygen-source.json", src=self.source_folder,
             dst=str(Path(self.package_folder) / "share" / "oxygen"))

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
        """Deploy Oxygen's dependency interface, not upstream package mirrors."""
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
