# pyright: strict
"""Source provenance: version/commit discovery, the source capsule and version builds.

Binaries carry only the semantic version. The source commit and dirty state live
in the ``oxygen-source.json`` capsule, so a commit or an edit never changes the
compiled SDK.
"""

import json
import os
import shutil
import unittest
from collections.abc import Mapping
from pathlib import Path
from typing import TypedDict

from cmake_testing import (
    CMAKE,
    CONAN,
    ENGINE,
    GIT,
    NATIVE_COMPILER,
    NATIVE_GENERATORS,
    NINJA,
    WINDOWS,
    WINDOWS_PROFILE,
    Argument,
    CommandTestCase,
    Result,
    include,
    mtimes,
    oxygen_recipe,
    read,
    read_json,
    requires_conan,
    skip_unless,
    write,
)

CORE = Path("src/Oxygen/Core")
VERSION_HEADER = Path("version/include/Oxygen/Core/version-info.h")
VERSION_CAPSULE = Path("version/oxygen-source.json")
FIXTURE_NAME = "'oxygen-provenance-fixture'"
# Provenance fixtures have no dependencies and never provision Python tooling.
FIXTURE_STUBS = (
    "requirements",
    "build_requirements",
    "_package_component_metadata",
    "_prepare_build_python",
    "package_info",
)
INVALID_VERSIONS = ("256.0.0", "0.256.0", "0.0.256", "01.2.3", "1.2", "1.2.3-dev")

VERSION_PROBE = """#include <Oxygen/Core/Version.h>
#include <iostream>
#include <type_traits>
static_assert(std::is_same_v<decltype(oxygen::version::Patch()), std::uint8_t>);
int main() { using namespace oxygen::version;
std::cout << Version() << "\\n" << NameVersion() << "\\n"
<< +Major() << "," << +Minor() << "," << +Patch() << "\\n"; }
"""

VERSION_PROJECT = """cmake_minimum_required(VERSION 4.2)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceProvenance.cmake")
asap_version_read()
project(VersionFixture VERSION ${META_VERSION} LANGUAGES CXX)
set(OXYGEN_PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OXYGEN_INSTALL ON)
set(META_PROJECT_NAME Oxygen)
set(META_PROJECT_ID_LOWER oxygen)
set(META_MODULE_NAME Oxygen.Core)
add_library(version_library STATIC src/Oxygen/Core/Version.cpp)
oxygen_add_version_metadata(version_library)
target_sources(version_library PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
  FILES src/Oxygen/Core/Version.h src/Oxygen/Core/api_export.h)
target_compile_features(version_library PUBLIC cxx_std_23)
add_executable(version_probe main.cpp)
target_link_libraries(version_probe PRIVATE version_library)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:version_probe>")
file(WRITE "${CMAKE_BINARY_DIR}/configured-version.txt" "${PROJECT_VERSION}")
install(TARGETS version_library version_probe FILE_SET HEADERS DESTINATION include)
"""


class Capsule(TypedDict):
    """The ``oxygen-source.json`` schema."""

    schema: int
    version: str
    commit: str | None
    dirty: bool | None


def capsule(version: str, commit: str | None, dirty: bool | None) -> Capsule:
    return {"schema": 1, "version": version, "commit": commit, "dirty": dirty}


@skip_unless(CMAKE and GIT, "CMake and Git are required")
class ProvenanceTestCase(CommandTestCase):
    """An engine source tree inside a throwaway Git repository."""

    temp_prefix = "oxygen provenance "

    def setUp(self) -> None:
        super().setUp()
        self.repo = self.root / "repo"
        self.source = self.repo / "projects/Oxygen.Engine"
        write(self.source / "VERSION", "7.8.9\n")
        write(self.source / "CMakeLists.txt", "# source fixture\n")
        write(self.source / ".gitignore", "out/\nversion-info.h\n")
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Provenance Test")
        self.git("config", "user.email", "provenance@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.initial = self.commit()

    def git(self, *arguments: Argument, cwd: Path | None = None) -> str:
        return self.run_command([GIT, *arguments], cwd=cwd or self.repo).stdout.strip()

    def commit(self) -> str:
        self.git("add", "--", ".", ":(exclude)projects/Oxygen.Engine/plans/CMAKE_CONAN_MODERNIZATION_PLAN.md")
        self.git("-c", "core.hooksPath=/dev/null", "commit", "-m", "fixture")
        return self.git("rev-parse", "HEAD")

    def read_provenance(self, source: Path | None, *, success: bool) -> Path:
        """Run CMake's provenance reader on ``source``; returns where it writes the capsule."""
        result_file = self.root / "source.json"
        self.cmake_script(
            include("SourceProvenance.cmake")
            + f'oxygen_read_source_provenance("{(source or self.source).as_posix()}" OXYGEN_SOURCE)\n'
            f'file(WRITE "{result_file.as_posix()}" "${{OXYGEN_SOURCE_JSON}}")\n',
            name="read.cmake",
            success=success,
        )
        return result_file

    def provenance(self, source: Path | None = None) -> Capsule:
        """The provenance that CMake reads for ``source`` (default: the fixture tree)."""
        result: Capsule = read_json(self.read_provenance(source, success=True))
        return result

    def assert_provenance_rejected(self, source: Path | None = None) -> None:
        self.read_provenance(source, success=False)

    def set_version(self, value: str) -> None:
        write(self.source / "VERSION", value)

    def conan_environment(self, name: str) -> dict[str, str]:
        """An isolated Conan home with Oxygen's custom settings."""
        home = self.root / name
        home.mkdir(exist_ok=True)
        shutil.copyfile(ENGINE / "conan-settings_user.yml", home / "settings_user.yml")
        return {**os.environ, "CONAN_HOME": str(home)}

    def conan(self, *arguments: Argument, env: Mapping[str, str], success: bool = True) -> Result:
        return self.run_command([CONAN, *arguments], env=env, success=success)

    def cache_path(self, reference: str, env: Mapping[str, str], *arguments: str) -> Path:
        return Path(self.conan("cache", "path", reference, *arguments, env=env).stdout.strip())

    def write_recipe(self, **attributes: str) -> None:
        write(self.source / "conanfile.py", oxygen_recipe(attributes=attributes, stubs=FIXTURE_STUBS))


class ProvenanceTests(ProvenanceTestCase):
    def test_component_history_dirty_inputs_and_exclusions(self) -> None:
        self.assertEqual(self.provenance(), capsule("7.8.9", self.initial, False))
        write(self.repo / "editor.txt", "unrelated\n")
        self.commit()
        self.assertEqual(self.provenance()["commit"], self.initial)
        write(self.source / "plans/CMAKE_CONAN_MODERNIZATION_PLAN.md", "uncommitted plan\n")
        write(self.source / "out/generated.h", "generated\n")
        self.assertFalse(self.provenance()["dirty"])
        new_input = write(self.source / "new.cpp", "// relevant new input\n")
        self.assertTrue(self.provenance()["dirty"])
        new_input.unlink()
        self.set_version("7.8.10\n")
        self.assertTrue(self.provenance()["dirty"])
        revision = self.commit()
        self.assertEqual(self.provenance()["commit"], revision)
        self.assertFalse(self.provenance()["dirty"])

    def test_worktree_detached_head_packed_refs_and_merge(self) -> None:
        worktree = self.root / "linked tree"
        linked_source = worktree / "projects/Oxygen.Engine"
        self.git("worktree", "add", "--detach", worktree, "HEAD")
        self.assertEqual(self.provenance(linked_source)["commit"], self.initial)
        self.git("pack-refs", "--all")
        self.assertEqual(self.provenance(linked_source)["commit"], self.initial)
        self.git("switch", "-c", "feature")
        write(self.source / "feature.cpp", "// source\n")
        revision = self.commit()
        self.git("switch", "main")
        write(self.repo / "other.txt", "other project\n")
        self.commit()
        self.git("-c", "core.hooksPath=/dev/null", "merge", "--no-ff", "feature", "-m", "merge")
        self.assertEqual(self.provenance()["commit"], revision)
        self.git("checkout", "--detach", revision)
        self.assertEqual(self.provenance()["commit"], revision)

    def test_shallow_history_and_untracked_archive_are_unknown(self) -> None:
        shallow = self.root / "shallow"
        self.git("clone", "--depth=1", self.repo.as_uri(), shallow)
        self.assertIsNone(self.provenance(shallow / "projects/Oxygen.Engine")["commit"])
        archive = self.repo / "vendor/archive"
        archive.mkdir(parents=True)
        for name in ("VERSION", "CMakeLists.txt"):
            shutil.copyfile(self.source / name, archive / name)
        self.assertIsNone(self.provenance(archive)["dirty"])

    def test_captured_provenance_takes_precedence_and_validates_schema(self) -> None:
        data = self.provenance()
        captured = self.source / "oxygen-source.json"

        def capture(value: Mapping[str, object]) -> None:
            write(captured, json.dumps(value))

        data["dirty"] = True
        capture(data)
        self.assertEqual(self.provenance(), data)
        sha256: Capsule = {**data, "commit": "a" * 64}
        capture(sha256)
        self.assertEqual(self.provenance(), sha256)
        for field, value in (
            ("schema", True),
            ("schema", 2),
            ("version", "1.2.3"),
            ("commit", "abcd"),
            ("dirty", "false"),
        ):
            with self.subTest(field=field, value=value):
                capture({**data, field: value})
                self.assert_provenance_rejected()
        invalid_capsules: list[dict[str, object]] = [{**data, "extra": 1}, {**data, "commit": None}, {}]
        for invalid in invalid_capsules:
            with self.subTest(invalid=invalid):
                capture(invalid)
                self.assert_provenance_rejected()
        unknown: Capsule = {**data, "commit": None, "dirty": None}
        capture(unknown)
        self.assertEqual(self.provenance(), unknown)

    def test_version_overrides_and_limits_without_configuring_a_compiler(self) -> None:
        other = write(self.root / "alternate version", "255.0.255\n")
        for path in (other.as_posix(), other.name):
            with self.subTest(path=path):
                self.cmake_script(
                    include("VersionHelpers.cmake") + f'asap_version_read(VERSION_FILE "{path}")\n'
                    'if(NOT META_VERSION STREQUAL "255.0.255")\nmessage(FATAL_ERROR "Override ignored")\nendif()\n',
                    name="version.cmake",
                )
        for value in ("0.0.0", "255.255.255", "1.2.3"):
            self.set_version(value + "\n")
            self.assertEqual(self.provenance()["version"], value)
        for value in (*INVALID_VERSIONS, "-1.2.3", "9" * 100 + ".0.0"):
            with self.subTest(version=value):
                self.set_version(value)
                self.assert_provenance_rejected()
        self.cmake_script(
            include("VersionHelpers.cmake") + 'asap_version_read(VERSION_FILE "")\n',
            name="version.cmake",
            success=False,
        )

    def test_preproject_provenance_preserves_toolchain_scope(self) -> None:
        toolchain = write(self.root / "toolchain.cmake", 'include_guard()\nset(OXYGEN_TOOLCHAIN_PROOF "visible")\n')
        write(
            self.source / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 4.2)\n"
            + include("SourceProvenance.cmake")
            + 'oxygen_read_source_provenance("${CMAKE_CURRENT_SOURCE_DIR}" OXYGEN_SOURCE)\n'
            "project(ToolchainScope NONE)\n"
            'if(NOT OXYGEN_TOOLCHAIN_PROOF STREQUAL "visible")\n'
            'message(FATAL_ERROR "Toolchain normal variables were lost before project()")\nendif()\n',
        )
        self.configure(self.source, self.root / "bootstrap", NINJA, f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}")

    @requires_conan
    def test_conan_export_parity_and_stable_revision_without_building(self) -> None:
        self.write_recipe(name=FIXTURE_NAME, settings="()")
        self.commit()
        env = self.conan_environment("conan-cache")

        def export() -> str:
            reference: str = json.loads(self.conan("export", self.source, "--format=json", env=env).stdout)["reference"]
            return reference

        def assert_export_rejected(*extra: str) -> None:
            self.conan("export", self.source, *extra, env=env, success=False)

        reference = export()
        captured = json.loads(read(self.cache_path(reference, env) / "oxygen-source.json"))
        self.assertEqual(captured, self.provenance())
        write(self.source / CORE / "version-info.h", "#error stale generated header\n")
        write(self.repo / "unrelated.txt", "other project\n")
        self.commit()
        self.assertEqual(export(), reference)
        write(self.source / "new.cpp", "// local work\n")
        dirty = export()
        self.assertNotEqual(dirty, reference)
        exported = self.cache_path(dirty, env, "--folder=export_source")
        self.assertEqual(self.provenance(exported), self.provenance())
        self.assertFalse((exported / CORE / "version-info.h").exists())
        self.assertFalse((self.source / "oxygen-source.json").exists())
        assert_export_rejected("--version=1.2.3")
        for value in INVALID_VERSIONS:
            with self.subTest(version=value):
                self.set_version(value)
                assert_export_rejected()

    @requires_conan
    def test_local_install_does_not_freeze_provenance(self) -> None:
        self.write_recipe()
        shutil.copyfile(ENGINE / "CMakePresets.json", self.source / "CMakePresets.json")
        write(self.source / ".gitignore", "out/\nCMakeUserPresets.json\n")
        self.commit()
        self.conan(
            "install",
            self.source,
            "-pr:h",
            WINDOWS_PROFILE,
            "-pr:b",
            WINDOWS_PROFILE,
            "-s:h",
            "build_type=Debug",
            "-s:b",
            "build_type=Release",
            "-c",
            "tools.cmake.cmaketoolchain:generator=Ninja",
            "--build=never",
            "--no-remote",
            env=self.conan_environment("install-cache"),
        )
        self.assertFalse((self.source / "oxygen-source.json").exists())
        self.assertFalse(self.provenance()["dirty"])
        self.set_version("7.8.10\n")
        self.assertEqual(self.provenance()["version"], "7.8.10")
        self.assertTrue(self.provenance()["dirty"])


@skip_unless(NATIVE_COMPILER, "A native compiler (x64 developer shell) is required")
class VersionBuildTests(ProvenanceTestCase):
    """Builds the production Version.cpp in a fixture, without engine dependencies."""

    def setUp(self) -> None:
        super().setUp()
        core = self.source / CORE
        core.mkdir(parents=True)
        for name in ("Version.h", "Version.cpp"):
            shutil.copyfile(ENGINE / CORE / name, core / name)
        # Only the export annotation is stubbed; compile the production Version.cpp.
        write(core / "api_export.h", "#define OXGN_CORE_NDAPI\n")
        write(core / "version-info.h", "#error stale source header must not be included\n")
        shutil.copyfile(ENGINE / CORE / "version.h.in", self.source / "version.h.in")
        modules = self.source / "cmake"
        modules.mkdir()
        for name in (
            "SourceProvenance.cmake",
            "VersionHelpers.cmake",
            "VersionConfig.cmake.in",
            "GenerateVersion.cmake",
        ):
            shutil.copyfile(ENGINE / "cmake" / name, modules / name)
        write(self.source / "main.cpp", VERSION_PROBE)
        write(self.source / "CMakeLists.txt", VERSION_PROJECT)
        self.revision = self.commit()

    def configure_version(
        self, source: Path, build: Path, generator: str = NINJA, *, without_git: bool = False
    ) -> None:
        self.configure(
            source,
            build,
            generator,
            "-DCMAKE_BUILD_TYPE=Debug",
            *(["-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE"] if without_git else []),
        )

    def build_and_check(self, build: Path, version: str, commit: str | None, dirty: bool | None) -> None:
        """Build, run the probe, and check the binary and the capsule separately."""
        self.build(build, "--parallel", "1")
        probe = read(build / "probe-Debug.txt")
        self.assertEqual(
            self.run_command([probe]).stdout.splitlines(), [version, f"Oxygen v{version}", version.replace(".", ",")]
        )
        if commit:
            self.assertNotIn(commit, read(build / VERSION_HEADER))
        self.assertEqual(json.loads(read(build / VERSION_CAPSULE)), capsule(version, commit, dirty))

    def compiled_version(self, build: Path) -> dict[Path, int]:
        """Timestamps of the generated header and every compiled Version object."""
        objects = [
            path for path in build.rglob("*") if path.name in ("Version.obj", "Version.cpp.obj", "Version.cpp.o")
        ]
        self.assertTrue(objects, "No compiled Version object found")
        return mtimes([build / VERSION_HEADER, *objects])

    def test_incremental_version_api_and_independent_trees(self) -> None:
        revision = self.revision
        for index, generator in enumerate(NATIVE_GENERATORS):
            with self.subTest(generator=generator):
                build = self.root / f"build-{index}"
                self.configure_version(self.source, build, generator)
                self.build_and_check(build, "7.8.9", revision, False)
                compiled = self.compiled_version(build)
                self.build_and_check(build, "7.8.9", revision, False)
                write(self.repo / "editor.txt", f"unrelated {index}\n")
                self.commit()
                self.build_and_check(build, "7.8.9", revision, False)
                # Relevant edits and commits update the capsule only.
                write(self.source / "new.cpp", f"// relevant {index}\n")
                self.build_and_check(build, "7.8.9", revision, True)
                revision = self.commit()
                self.build_and_check(build, "7.8.9", revision, False)
                self.assertEqual(self.compiled_version(build), compiled)
        first_header = self.root / "build-0" / VERSION_HEADER
        before = first_header.read_bytes()
        last = self.root / f"build-{len(NATIVE_GENERATORS) - 1}"
        self.set_version("255.0.255\n")
        self.build_and_check(last, "255.0.255", revision, True)
        self.assertEqual(read(last / "configured-version.txt"), "255.0.255")
        self.assertEqual(first_header.read_bytes(), before)
        self.assertEqual(
            read(self.source / CORE / "version-info.h"), "#error stale source header must not be included\n"
        )

    @requires_conan
    def test_build_exported_cache_sources_and_install_provenance(self) -> None:
        self.write_recipe(
            name=FIXTURE_NAME, exports_sources='(*module.OxygenConan.exports_sources, "version.h.in", "main.cpp")'
        )
        revision = self.commit()
        env = self.conan_environment("conan-cache")
        for dirty in (False, True):
            with self.subTest(dirty=dirty):
                if dirty:
                    write(self.source / "new.cpp", "// modified input\n")
                reference = json.loads(self.conan("export", self.source, "--format=json", env=env).stdout)["reference"]
                exported = self.cache_path(reference, env, "--folder=export_source")
                build = self.root / f"cache-build-{dirty}"
                self.configure_version(exported, build, without_git=True)
                self.build_and_check(build, "7.8.9", revision, dirty)
                prefix = self.root / f"installed-{dirty}"
                self.run_command([CMAKE, "--install", build, "--config", "Debug", "--prefix", prefix])
                self.assertEqual(
                    json.loads(read(prefix / "share/oxygen/oxygen-source.json")), self.provenance(exported)
                )
                self.assertEqual(
                    (prefix / "include/Oxygen/Core/version-info.h").read_bytes(), (build / VERSION_HEADER).read_bytes()
                )
        if WINDOWS:
            self.check_conan_create(env)
        archive = self.root / "archive"
        shutil.copytree(self.source, archive)
        build = self.root / "unknown-build"
        self.configure_version(archive, build)
        self.build_and_check(build, "7.8.9", None, None)

    def check_conan_create(self, env: Mapping[str, str]) -> None:
        result = self.conan(
            "create",
            self.source,
            "-pr:h",
            WINDOWS_PROFILE,
            "-pr:b",
            WINDOWS_PROFILE,
            "-s:h",
            "build_type=Debug",
            "-s:b",
            "build_type=Release",
            "-s:h",
            "compiler.cppstd=23",
            "-c",
            "tools.build:jobs=1",
            "-c",
            "tools.cmake.cmaketoolchain:generator=Ninja",
            # Conan runs this through the shell without quoting it.
            "-c",
            f'tools.cmake:cmake_program="{CMAKE}"',
            "--no-remote",
            "--build=missing",
            "-tf",
            "",
            "--format=json",
            env=env,
        )
        nodes = json.loads(result.stdout)["graph"]["nodes"].values()
        node = next(n for n in nodes if n["name"] == "oxygen-provenance-fixture" and n.get("package_id"))
        package = self.cache_path(f"{node['ref']}:{node['package_id']}", env)
        self.assertEqual(json.loads(read(package / "share/oxygen/oxygen-source.json")), self.provenance())
        self.assertEqual(
            self.run_command([package / "bin/version_probe.exe"]).stdout.splitlines(),
            ["7.8.9", "Oxygen v7.8.9", "7,8,9"],
        )

    def test_shared_library_preserves_its_link_policy(self) -> None:
        project = self.source / "CMakeLists.txt"
        write(
            project,
            read(project).replace("add_library(version_library STATIC", "add_library(version_library SHARED")
            + "set_property(TARGET version_library PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)\n"
            "get_target_property(_interface version_library INTERFACE_LINK_OPTIONS)\n"
            'if(_interface MATCHES "INCREMENTAL:NO")\n'
            '  message(FATAL_ERROR "Static Core policy leaked to shared Core")\nendif()\n',
        )
        revision = self.commit()
        build = self.root / "shared-build"
        self.configure_version(self.source, build)
        self.build_and_check(build, "7.8.9", revision, False)
        for index in range(3):
            write(self.source / "change.txt", f"shared change {index}\n")
            revision = self.commit()
            self.build_and_check(build, "7.8.9", revision, False)


if __name__ == "__main__":
    unittest.main()
