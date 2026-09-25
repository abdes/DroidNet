"""Version/provenance checks. Build tests require explicit opt-in on shared hosts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_build_contract import CMAKE, CONAN, ENGINE, CommandTests


@unittest.skipUnless(CMAKE and shutil.which("git"), "CMake and Git are required")
class ProvenanceFixture(CommandTests):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="oxygen provenance ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.source = self.repo / "projects/Oxygen.Engine"
        self.source.mkdir(parents=True)
        (self.source / "VERSION").write_text("7.8.9\n", encoding="utf-8")
        (self.source / "CMakeLists.txt").write_text("# source fixture\n", encoding="utf-8")
        (self.source / ".gitignore").write_text("out/\nversion-info.h\n", encoding="utf-8")
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Provenance Test")
        self.git("config", "user.email", "provenance@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.initial = self.commit()

    def git(self, *arguments, root=None):
        return self.run_command(["git", *arguments], root or self.repo).stdout.strip()

    def commit(self):
        self.git("add", "--", ".", ":(exclude)projects/Oxygen.Engine/plans/CMAKE_CONAN_MODERNIZATION_PLAN.md")
        self.git("-c", "core.hooksPath=/dev/null", "commit", "-m", "fixture")
        return self.git("rev-parse", "HEAD")

    def read(self, source=None, *, success=True):
        source = source or self.source
        script = self.root / "read.cmake"
        output = self.root / "source.json"
        script.write_text(
            'cmake_minimum_required(VERSION 4.2)\n'
            f'include("{ENGINE.as_posix()}/cmake/SourceProvenance.cmake")\n'
            f'oxygen_read_source_provenance("{source.as_posix()}" OXYGEN_SOURCE)\n'
            f'file(WRITE "{output.as_posix()}" "${{OXYGEN_SOURCE_JSON}}")\n', encoding="utf-8",
        )
        result = self.run_command([CMAKE, "-P", str(script)], self.root, success=success)
        return json.loads(output.read_text()) if success else result


class ProvenanceTests(ProvenanceFixture):
    def test_component_history_dirty_inputs_and_exclusions(self):
        self.assertEqual(self.read(), {"schema": 1, "version": "7.8.9", "commit": self.initial, "dirty": False})
        (self.repo / "editor.txt").write_text("unrelated\n")
        self.commit()
        self.assertEqual(self.read()["commit"], self.initial)
        plans = self.source / "plans"
        plans.mkdir()
        (plans / "CMAKE_CONAN_MODERNIZATION_PLAN.md").write_text("uncommitted plan\n")
        (self.source / "out").mkdir()
        (self.source / "out/generated.h").write_text("generated\n")
        self.assertFalse(self.read()["dirty"])
        (self.source / "new.cpp").write_text("// relevant new input\n")
        self.assertTrue(self.read()["dirty"])
        (self.source / "new.cpp").unlink()
        (self.source / "VERSION").write_text("7.8.10\n")
        self.assertTrue(self.read()["dirty"])
        revision = self.commit()
        self.assertEqual(self.read()["commit"], revision)
        self.assertFalse(self.read()["dirty"])

    def test_worktree_detached_head_packed_refs_and_merge(self):
        worktree = self.root / "linked tree"
        self.git("worktree", "add", "--detach", str(worktree), "HEAD")
        self.assertEqual(self.read(worktree / "projects/Oxygen.Engine")["commit"], self.initial)
        self.git("pack-refs", "--all")
        self.assertEqual(self.read(worktree / "projects/Oxygen.Engine")["commit"], self.initial)
        self.git("switch", "-c", "feature")
        (self.source / "feature.cpp").write_text("// source\n")
        revision = self.commit()
        self.git("switch", "main")
        (self.repo / "other.txt").write_text("other project\n")
        self.commit()
        self.git("-c", "core.hooksPath=/dev/null", "merge", "--no-ff", "feature", "-m", "merge")
        self.assertEqual(self.read()["commit"], revision)
        self.git("checkout", "--detach", revision)
        self.assertEqual(self.read()["commit"], revision)

    def test_shallow_history_and_untracked_archive_are_unknown(self):
        shallow = self.root / "shallow"
        self.git("clone", "--depth=1", self.repo.as_uri(), str(shallow))
        self.assertIsNone(self.read(shallow / "projects/Oxygen.Engine")["commit"])
        archive = self.repo / "vendor/archive"
        archive.mkdir(parents=True)
        shutil.copyfile(self.source / "VERSION", archive / "VERSION")
        shutil.copyfile(self.source / "CMakeLists.txt", archive / "CMakeLists.txt")
        self.assertIsNone(self.read(archive)["dirty"])

    def test_captured_provenance_takes_precedence_and_validates_schema(self):
        data = self.read()
        capsule = self.source / "oxygen-source.json"
        data["dirty"] = True
        capsule.write_text(json.dumps(data))
        self.assertEqual(self.read(), data)
        sha256 = {**data, "commit": "a" * 64}
        capsule.write_text(json.dumps(sha256))
        self.assertEqual(self.read(), sha256)
        for field, value in (("schema", True), ("schema", 2), ("version", "1.2.3"),
                             ("commit", "abcd"), ("dirty", "false")):
            with self.subTest(field=field, value=value):
                invalid = dict(data)
                invalid[field] = value
                capsule.write_text(json.dumps(invalid))
                self.read(success=False)
        for invalid in (dict(data, extra=1), {**data, "commit": None}, {}):
            capsule.write_text(json.dumps(invalid))
            self.read(success=False)
        unknown = {**data, "commit": None, "dirty": None}
        capsule.write_text(json.dumps(unknown))
        self.assertEqual(self.read(), unknown)

    def test_version_overrides_and_limits_without_configuring_a_compiler(self):
        script = self.root / "version.cmake"
        other = self.root / "alternate version"
        other.write_text("255.0.255\n")
        for path in (other.as_posix(), other.name):
            script.write_text(
                'cmake_minimum_required(VERSION 4.2)\n'
                f'include("{ENGINE.as_posix()}/cmake/VersionHelpers.cmake")\n'
                f'asap_version_read(VERSION_FILE "{path}")\n'
                'if(NOT META_VERSION STREQUAL "255.0.255")\nmessage(FATAL_ERROR "Override ignored")\nendif()\n',
                encoding="utf-8",
            )
            self.run_command([CMAKE, "-P", str(script)], self.root)
        for value in ("0.0.0", "255.255.255", "1.2.3"):
            (self.source / "VERSION").write_text(value + "\n")
            self.assertEqual(self.read()["version"], value)
        for value in ("256.0.0", "0.256.0", "0.0.256", "01.2.3", "1.2", "-1.2.3", "1.2.3-dev", "9" * 100 + ".0.0"):
            (self.source / "VERSION").write_text(value)
            self.read(success=False)
        script.write_text(
            'cmake_minimum_required(VERSION 4.2)\n'
            f'include("{ENGINE.as_posix()}/cmake/VersionHelpers.cmake")\n'
            'asap_version_read(VERSION_FILE "")\n', encoding="utf-8",
        )
        self.run_command([CMAKE, "-P", str(script)], self.root, success=False)

    def test_preproject_provenance_preserves_toolchain_scope(self):
        toolchain = self.root / "toolchain.cmake"
        toolchain.write_text('include_guard()\nset(OXYGEN_TOOLCHAIN_PROOF "visible")\n')
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\n'
            f'include("{ENGINE.as_posix()}/cmake/SourceProvenance.cmake")\n'
            'oxygen_read_source_provenance("${CMAKE_CURRENT_SOURCE_DIR}" OXYGEN_SOURCE)\n'
            'project(ToolchainScope NONE)\n'
            'if(NOT OXYGEN_TOOLCHAIN_PROOF STREQUAL "visible")\n'
            'message(FATAL_ERROR "Toolchain normal variables were lost before project()")\nendif()\n',
            encoding="utf-8",
        )
        self.run_command([CMAKE, "-S", str(self.source), "-B", str(self.root / "bootstrap"),
                          "-G", "Ninja", f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}"], self.root)

    @unittest.skipUnless(CONAN, "Conan is required")
    def test_conan_export_parity_and_stable_revision_without_building(self):
        (self.source / "conanfile.py").write_text(
            'import importlib.util\n'
            f'spec = importlib.util.spec_from_file_location("oxygen_recipe", {str(ENGINE / "conanfile.py")!r})\n'
            'module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n'
            'class Fixture(module.OxygenConan):\n'
            '    name = "oxygen-provenance-fixture"\n'
            '    settings = ()\n'
            '    def requirements(self): pass\n'
            '    def build_requirements(self): pass\n'
            '    def _package_component_metadata(self): pass\n'
            '    def package_info(self): pass\n', encoding="utf-8",
        )
        self.commit()
        environment = {**os.environ, "CONAN_HOME": str(self.root / "conan-cache")}

        def export(*extra, success=True):
            result = self.run_command([CONAN, "export", str(self.source), "--format=json", *extra],
                                      self.root, env=environment, success=success)
            return json.loads(result.stdout) if success else result

        first = export()
        reference = first["reference"]
        folder = self.run_command([CONAN, "cache", "path", reference], self.root, env=environment).stdout.strip()
        captured = json.loads((Path(folder) / "oxygen-source.json").read_text())
        self.assertEqual(captured, self.read())
        stale = self.source / "src/Oxygen/Core/version-info.h"
        stale.parent.mkdir(parents=True)
        stale.write_text("#error stale generated header\n")
        (self.repo / "unrelated.txt").write_text("other project\n")
        self.commit()
        self.assertEqual(export()["reference"], reference)
        (self.source / "new.cpp").write_text("// local work\n")
        dirty = export()
        self.assertNotEqual(dirty["reference"], reference)
        folder = self.run_command([CONAN, "cache", "path", dirty["reference"], "--folder=export_source"],
                                  self.root, env=environment).stdout.strip()
        self.assertEqual(self.read(Path(folder)), self.read())
        self.assertFalse((Path(folder) / "src/Oxygen/Core/version-info.h").exists())
        self.assertFalse((self.source / "oxygen-source.json").exists())
        export("--version=1.2.3", success=False)
        for value in ("256.0.0", "0.256.0", "0.0.256", "01.2.3", "1.2", "1.2.3-dev"):
            (self.source / "VERSION").write_text(value)
            export(success=False)


    @unittest.skipUnless(CONAN, "Conan is required")
    def test_local_install_does_not_freeze_provenance(self):
        (self.source / "conanfile.py").write_text(
            'import importlib.util\n'
            f'spec = importlib.util.spec_from_file_location("oxygen_recipe", {str(ENGINE / "conanfile.py")!r})\n'
            'module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n'
            'class Fixture(module.OxygenConan):\n'
            '    def requirements(self): pass\n'
            '    def build_requirements(self): pass\n'
            '    def _package_component_metadata(self): pass\n'
            '    def package_info(self): pass\n', encoding="utf-8",
        )
        shutil.copyfile(ENGINE / "CMakePresets.json", self.source / "CMakePresets.json")
        (self.source / ".gitignore").write_text("out/\nCMakeUserPresets.json\n")
        self.commit()
        home = self.root / "install-cache"
        home.mkdir()
        shutil.copyfile(ENGINE / "conan-settings_user.yml", home / "settings_user.yml")
        environment = {**os.environ, "CONAN_HOME": str(home)}
        profile = str(ENGINE / "profiles/windows-msvc.ini")
        self.run_command([CONAN, "install", str(self.source), "-pr:h", profile, "-pr:b", profile,
                          "-s:h", "build_type=Debug", "-s:b", "build_type=Release",
                          "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
                          "--build=never", "--no-remote"], self.root, env=environment)
        self.assertFalse((self.source / "oxygen-source.json").exists())
        self.assertFalse(self.read()["dirty"])
        (self.source / "VERSION").write_text("7.8.10\n")
        self.assertEqual(self.read()["version"], "7.8.10")
        self.assertTrue(self.read()["dirty"])


@unittest.skipUnless(os.environ.get("OXYGEN_RUN_PROVENANCE_BUILD_TESTS") == "1",
                     "Builds require OXYGEN_RUN_PROVENANCE_BUILD_TESTS=1 and permission on shared hosts")
class VersionBuildTests(ProvenanceFixture):
    def prepare(self):
        core = self.source / "src/Oxygen/Core"
        core.mkdir(parents=True)
        for name in ("Version.h", "Version.cpp"):
            shutil.copyfile(ENGINE / "src/Oxygen/Core" / name, core / name)
        # Only the export annotation is stubbed; compile the production Version.cpp.
        (core / "api_export.h").write_text("#define OXGN_CORE_NDAPI\n")
        (core / "version-info.h").write_text("#error stale source header must not be included\n")
        shutil.copyfile(ENGINE / "src/Oxygen/Core/version.h.in", self.source / "version.h.in")
        modules = self.source / "cmake"
        modules.mkdir()
        for name in ("SourceProvenance.cmake", "VersionHelpers.cmake", "VersionConfig.cmake.in", "GenerateVersion.cmake"):
            shutil.copyfile(ENGINE / "cmake" / name, modules / name)
        (self.source / "main.cpp").write_text(
            '#include <Oxygen/Core/Version.h>\n#include <iostream>\n#include <type_traits>\n'
            'static_assert(std::is_same_v<decltype(oxygen::version::Patch()), std::uint8_t>);\n'
            'int main() { using namespace oxygen::version;\n'
            'std::cout << Version() << "\\n" << VersionFull() << "\\n" << NameVersion() << "\\n"\n'
            '<< +Major() << "," << +Minor() << "," << +Patch() << "\\n"; }\n', encoding="utf-8",
        )
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\n'
            'include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceProvenance.cmake")\n'
            'asap_version_read()\nproject(VersionFixture VERSION ${META_VERSION} LANGUAGES CXX)\n'
            'set(OXYGEN_PROJECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")\n'
            'set(OXYGEN_INSTALL ON)\nset(META_PROJECT_NAME Oxygen)\n'
            'set(META_PROJECT_ID_LOWER oxygen)\nset(META_MODULE_NAME Oxygen.Core)\n'
            'add_library(version_library STATIC src/Oxygen/Core/Version.cpp)\n'
            'oxygen_add_version_metadata(version_library)\n'
            'target_sources(version_library PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"\n'
            ' FILES src/Oxygen/Core/Version.h src/Oxygen/Core/api_export.h)\n'
            'target_compile_features(version_library PUBLIC cxx_std_23)\n'
            'add_executable(version_probe main.cpp)\n'
            'target_link_libraries(version_probe PRIVATE version_library)\n'
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:version_probe>")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/configured-version.txt" "${PROJECT_VERSION}")\n'
            'install(TARGETS version_library version_probe FILE_SET HEADERS DESTINATION include)\n',
            encoding="utf-8",
        )
        return self.commit()

    def configure(self, source, build, generator, *, without_git=False):
        self.run_command([CMAKE, "-S", str(source), "-B", str(build), "-G", generator,
                          "-DCMAKE_BUILD_TYPE=Debug", *(["-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE"] if without_git else [])], self.root)

    def build_and_check(self, build, version, revision):
        result = self.run_command([CMAKE, "--build", str(build), "--config", "Debug", "--parallel", "1"], self.root)
        executable = (build / "probe-Debug.txt").read_text()
        actual = self.run_command([executable], self.root).stdout.splitlines()
        short = "unknown" if revision == "unknown" else revision[:12] + ("-dirty" if revision.endswith("-dirty") else "")
        if actual != [version, f"{version} ({revision})", f"Oxygen v{version} ({short})", version.replace(".", ",")]:
            diagnostic = []
            needle = revision.removesuffix("-dirty").encode()
            for path in build.rglob("*"):
                if path.suffix in (".obj", ".lib", ".exe") and ("Version" in path.name or "version_" in path.name):
                    diagnostic.append(f"{path.name}: expected revision bytes={needle in path.read_bytes()}")
            result.stdout += "\n" + "\n".join(diagnostic) + "\n" + (build / "version/include/Oxygen/Core/version-info.h").read_text()
        self.assertEqual(actual, [version, f"{version} ({revision})", f"Oxygen v{version} ({short})", version.replace(".", ",")],
                         result.stdout + result.stderr + (build / "version/oxygen-source.json").read_text())

    def timestamps(self, build):
        files = [build / "version/include/Oxygen/Core/version-info.h", build / "version/oxygen-source.json"]
        objects = [p for p in build.rglob("*") if p.suffix in (".obj", ".o") and p.name in ("Version.obj", "Version.cpp.obj", "Version.cpp.o")]
        self.assertTrue(objects, "No compiled Version object found")
        return {p: p.stat().st_mtime_ns for p in files + objects}

    def test_incremental_version_api_and_independent_trees(self):
        revision = self.prepare()
        generators = ["Ninja", "Ninja Multi-Config"]
        if os.name == "nt":
            generators.append("Visual Studio 18 2026")
        for index, generator in enumerate(generators):
            with self.subTest(generator=generator):
                build = self.root / f"build-{index}"
                self.configure(self.source, build, generator)
                self.build_and_check(build, "7.8.9", revision)
                stamps = self.timestamps(build)
                self.build_and_check(build, "7.8.9", revision)
                self.assertEqual(stamps, self.timestamps(build))
                (self.repo / "editor.txt").write_text(f"unrelated {index}\n")
                self.commit()
                self.build_and_check(build, "7.8.9", revision)
                self.assertEqual(stamps, self.timestamps(build))
                (self.source / "new.cpp").write_text(f"// relevant {index}\n")
                self.build_and_check(build, "7.8.9", revision + "-dirty")
                revision = self.commit()
                self.build_and_check(build, "7.8.9", revision)
        first_header = self.root / "build-0/version/include/Oxygen/Core/version-info.h"
        before = first_header.read_bytes()
        (self.source / "VERSION").write_text("255.0.255\n")
        self.build_and_check(self.root / f"build-{len(generators)-1}", "255.0.255", revision + "-dirty")
        self.assertEqual((self.root / f"build-{len(generators)-1}/configured-version.txt").read_text(), "255.0.255")
        self.assertEqual(first_header.read_bytes(), before)
        self.assertEqual((self.source / "src/Oxygen/Core/version-info.h").read_text(), "#error stale source header must not be included\n")

    @unittest.skipUnless(CONAN, "Conan is required")
    def test_build_exported_cache_sources_and_install_provenance(self):
        self.prepare()
        (self.source / "conanfile.py").write_text(
            'import importlib.util\n'
            f'spec = importlib.util.spec_from_file_location("oxygen_recipe", {str(ENGINE / "conanfile.py")!r})\n'
            'module = importlib.util.module_from_spec(spec)\nspec.loader.exec_module(module)\n'
            'class Fixture(module.OxygenConan):\n'
            '    name = "oxygen-provenance-fixture"\n'
            '    exports_sources = (*module.OxygenConan.exports_sources, "version.h.in", "main.cpp")\n'
            '    def requirements(self): pass\n'
            '    def build_requirements(self): pass\n'
            '    def _package_component_metadata(self): pass\n'
            '    def package_info(self): pass\n', encoding="utf-8",
        )
        revision = self.commit()
        home = self.root / "conan-cache"
        home.mkdir()
        shutil.copyfile(ENGINE / "conan-settings_user.yml", home / "settings_user.yml")
        environment = {**os.environ, "CONAN_HOME": str(home)}
        for dirty in (False, True):
            if dirty:
                (self.source / "new.cpp").write_text("// modified input\n")
            result = self.run_command([CONAN, "export", str(self.source), "--format=json"], self.root, env=environment)
            reference = json.loads(result.stdout)["reference"]
            source = Path(self.run_command([CONAN, "cache", "path", reference, "--folder=export_source"], self.root, env=environment).stdout.strip())
            build = self.root / f"cache-build-{dirty}"
            self.configure(source, build, "Ninja", without_git=True)
            self.build_and_check(build, "7.8.9", revision + ("-dirty" if dirty else ""))
            prefix = self.root / f"installed-{dirty}"
            self.run_command([CMAKE, "--install", str(build), "--config", "Debug", "--prefix", str(prefix)], self.root)
            self.assertEqual(json.loads((prefix / "share/oxygen/oxygen-source.json").read_text()), self.read(source))
            self.assertEqual((prefix / "include/Oxygen/Core/version-info.h").read_bytes(),
                             (build / "version/include/Oxygen/Core/version-info.h").read_bytes())
        if os.name == "nt":
            profile = str(ENGINE / "profiles/windows-msvc.ini")
            result = self.run_command([
                CONAN, "create", str(self.source), "-pr:h", profile, "-pr:b", profile,
                "-s:h", "build_type=Debug", "-s:b", "build_type=Release",
                "-s:h", "compiler.cppstd=23", "-c", "tools.build:jobs=1",
                "-c", "tools.cmake.cmaketoolchain:generator=Ninja",
                "-c", f"tools.cmake:cmake_program={CMAKE}",
                "--no-remote", "--build=missing", "-tf", "", "--format=json",
            ], self.root, env=environment)
            nodes = json.loads(result.stdout)["graph"]["nodes"].values()
            node = next(n for n in nodes if n["name"] == "oxygen-provenance-fixture" and n.get("package_id"))
            package_ref = f'{node["ref"]}:{node["package_id"]}'
            package = Path(self.run_command([CONAN, "cache", "path", package_ref],
                                           self.root, env=environment).stdout.strip())
            self.assertEqual(json.loads((package / "share/oxygen/oxygen-source.json").read_text()), self.read())
            actual = self.run_command([str(package / "bin/version_probe.exe")], self.root).stdout
            self.assertIn(f"7.8.9 ({revision}-dirty)", actual)
        archive = self.root / "archive"
        shutil.copytree(self.source, archive)
        build = self.root / "unknown-build"
        self.configure(archive, build, "Ninja")
        self.build_and_check(build, "7.8.9", "unknown")

    def test_shared_library_preserves_its_link_policy(self):
        self.prepare()
        cmakelists = self.source / "CMakeLists.txt"
        cmakelists.write_text(cmakelists.read_text().replace(
            'add_library(version_library STATIC', 'add_library(version_library SHARED') +
            '\nset_property(TARGET version_library PROPERTY WINDOWS_EXPORT_ALL_SYMBOLS ON)\n'
            'get_target_property(_interface version_library INTERFACE_LINK_OPTIONS)\n'
            'if(_interface MATCHES "INCREMENTAL:NO")\n'
            '  message(FATAL_ERROR "Static Core policy leaked to shared Core")\nendif()\n')
        revision = self.commit()
        build = self.root / "shared-build"
        self.configure(self.source, build, "Ninja")
        self.build_and_check(build, "7.8.9", revision)
        for index in range(3):
            (self.source / "change.txt").write_text(f"shared change {index}\n")
            revision = self.commit()
            self.build_and_check(build, "7.8.9", revision)


if __name__ == "__main__":
    unittest.main()
