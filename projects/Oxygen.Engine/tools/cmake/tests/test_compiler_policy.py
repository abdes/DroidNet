"""Build/run checks for private policy and sanitizer compile/link/runtime behavior."""

import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from test_build_contract import CMAKE, ENGINE, CommandTests


@unittest.skipUnless(CMAKE and (shutil.which("cl") if os.name == "nt" else shutil.which("c++")),
                     "A configured native compiler environment is required")
class CompilerPolicyTests(CommandTests):
    def write_project(self, root, *, asan=False, shared=False, strict=False, pic=None):
        (root / "oxygen").mkdir()
        (root / "oxygen/CMakeLists.txt").write_text(
            'set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)\n'
            f'add_library(subject {"SHARED" if shared else "STATIC"} subject.cpp warning.c)\n'
            'set_target_properties(subject PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON\n'
            '  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dependency-bin")\n'
            + ('set_property(TARGET subject PROPERTY COMPILE_WARNING_AS_ERROR ON)\n' if strict else ''),
            encoding="utf-8",
        )
        (root / "oxygen/subject.cpp").write_text(
            '#include <cstdlib>\n'
            '#if defined(_CPPRTTI) || defined(__GXX_RTTI)\n#error Oxygen must disable RTTI\n#endif\n'
            'extern "C" int exercise(int offset) {\n'
            '  volatile int* p = static_cast<int*>(std::malloc(4 * sizeof(int)));\n'
            '  p[0] = 42; int value = p[offset]; std::free(const_cast<int*>(p)); return value;\n}\n',
            encoding="utf-8",
        )
        (root / "oxygen/warning.c").write_text(
            'int warning_probe(void) { int intentionally_unused; return 0; }\n', encoding="utf-8",
        )
        (root / "main.cpp").write_text(
            '#include <cstdio>\n#include <cstring>\n'
            'extern "C" int exercise(int);\n'
            'struct A { virtual ~A() = default; }; struct B : A {};\n'
            'int main(int argc, char** argv) { B b; A* a = &b;\n'
            '  if (!dynamic_cast<B*>(a)) return 2;\n'
            '  if (argc > 1 && !std::strcmp(argv[1], "--gtest_list_tests")) {\n'
            '    if (exercise(0) != 42) return 3; std::puts("Policy.\\n  Clean"); return 0; }\n'
            '  return exercise(argc > 1 && !std::strcmp(argv[1], "bad") ? 12 : 0) == 42 ? 0 : 1;\n}\n',
            encoding="utf-8",
        )
        (root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.2)\nproject(Policy C CXX)\n'
            'enable_testing()\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n'
            f'set(OXYGEN_WITH_ASAN {"ON" if asan else "OFF"})\n'
            + (f'set(CMAKE_POSITION_INDEPENDENT_CODE {pic})\n' if pic is not None else '')
            + f'include("{ENGINE.as_posix()}/cmake/CompilerPolicy.cmake")\n'
            'if(MSVC)\nstring(APPEND CMAKE_EXE_LINKER_FLAGS " /OPT:NOREF")\nendif()\n'
            'add_subdirectory(oxygen)\n'
            'oxygen_apply_directory_build_policy("${CMAKE_CURRENT_SOURCE_DIR}/oxygen")\n'
            'add_library(GTest::gtest INTERFACE IMPORTED)\n'
            f'include("{ENGINE.as_posix()}/cmake/GTestHelpers.cmake")\n'
            'gtest_program(consumer SOURCES main.cpp DEPS subject)\n'
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/consumer-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:consumer>")\n'
            'get_target_property(_interface subject INTERFACE_COMPILE_OPTIONS)\n'
            'if(_interface)\nmessage(FATAL_ERROR "Private policy leaked: ${_interface}")\nendif()\n'
            'get_target_property(_pic subject POSITION_INDEPENDENT_CODE)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/pic.txt" "${_pic}")\n', encoding="utf-8",
        )

    def configure(self, root, generator="Ninja"):
        return self.run_command([CMAKE, "-S", str(root), "-B", str(root / "build"),
                                 "-G", generator, "-DCMAKE_BUILD_TYPE=Debug"], root)

    def test_private_warnings_rtti_and_module_error_opt_in(self):
        for strict in (False, True):
            with self.subTest(strict=strict), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.write_project(root, strict=strict)
                self.configure(root)
                build = self.run_command([CMAKE, "--build", "build", "--verbose"], root, success=not strict)
                self.assertIn("intentionally_unused", build.stdout + build.stderr)
                if not strict:
                    executable = (root / "build/consumer-Debug.txt").read_text()
                    self.run_command([executable], root)
                    commands = json.loads((root / "build/compile_commands.json").read_text())
                    own = next(c["command"] for c in commands if c["file"].endswith("subject.cpp"))
                    parent = next(c["command"] for c in commands if c["file"].endswith("main.cpp"))
                    self.assertIn("/W4" if os.name == "nt" else "-Wextra", own)
                    self.assertNotIn("/W4" if os.name == "nt" else "-Wextra", parent)

    @unittest.skipUnless(os.name == "nt", "Windows DLL search path regression")
    def test_discovery_with_multiple_transitive_runtime_directories(self):
        with tempfile.TemporaryDirectory(prefix="oxygen runtime ") as tmp:
            root = Path(tmp)
            self.write_project(root, shared=True)
            with (root / "oxygen/CMakeLists.txt").open("a", encoding="utf-8") as source:
                source.write(
                    'add_library(second SHARED second.cpp)\n'
                    'set_target_properties(second PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON\n'
                    '  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/second dependency")\n'
                    'target_link_libraries(subject PRIVATE second)\n')
            (root / "oxygen/second.cpp").write_text(
                'extern "C" int second_value() { return 42; }\n', encoding="utf-8")
            path = root / "oxygen/subject.cpp"
            path.write_text(path.read_text().replace(
                'extern "C" int exercise', 'extern "C" int second_value();\nextern "C" int exercise'
            ).replace('p[0] = 42;', 'p[0] = second_value();'), encoding="utf-8")
            self.configure(root, "Ninja Multi-Config")
            self.run_command([CMAKE, "--build", "build", "--config", "Debug"], root)
            ctest = str(Path(CMAKE).with_name("ctest.exe"))
            result = self.run_command([ctest, "--test-dir", "build", "-C", "Debug", "-R", "^Policy.Clean$", "-V"], root)
            self.assertIn("100% tests passed", result.stdout)

    def test_runtime_launcher_preserves_arguments_and_exit_status(self):
        with tempfile.TemporaryDirectory(prefix="oxygen arguments ") as tmp:
            root = Path(tmp)
            script = root / "launcher.cmake"
            script.write_text(
                f'set(OXYGEN_RUNTIME_COMMAND [==[{Path(CMAKE).as_posix()};-E;env;--]==])\n'
                f'include("{ENGINE.as_posix()}/cmake/RunTest.cmake")\n', encoding="utf-8")
            arguments = ["", "a;b", '"quoted"', "]==]", "trailing\\", "${not_expanded}"]
            result = self.run_command([
                CMAKE, "-P", str(script), "--", sys.executable, "-c",
                "import json,sys; print(json.dumps(sys.argv[1:])); sys.exit(37)",
                *arguments,
            ], root, success=False)
            self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout), arguments)

    def test_static_and_shared_asan_detection_and_test_discovery(self):
        generators = ["Ninja", "Ninja Multi-Config"]
        if os.name == "nt":
            generators.append("Visual Studio 18 2026")
        for generator in generators:
            for shared in (False, True):
                with self.subTest(generator=generator, shared=shared), tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    self.write_project(root, asan=True, shared=shared)
                    self.configure(root, generator)
                    build = self.run_command([CMAKE, "--build", "build", "--config", "Debug", "--verbose"], root)
                    self.assertIn("fsanitize=address", build.stdout)
                    if os.name == "nt" and generator.startswith("Ninja"):
                        self.assertIn("/OPT:NOREF", build.stdout)
                        self.assertIn("/INCREMENTAL:NO", build.stdout)
                        commands = json.loads((root / "build/compile_commands.json").read_text())
                        own = next(c["command"] for c in commands if c["file"].endswith("subject.cpp"))
                        self.assertNotIn("/RTC", own)
                        self.assertNotIn("/ZI", own)
                    ctest = str(Path(CMAKE).with_name("ctest.exe" if os.name == "nt" else "ctest"))
                    result = self.run_command([ctest, "--test-dir", "build", "-C", "Debug", "-R", "^Policy.Clean$", "-V"], root)
                    self.assertIn("100% tests passed", result.stdout)
                    executable = (root / "build/consumer-Debug.txt").read_text()
                    env = os.environ.copy()
                    if os.name == "nt":
                        env["PATH"] = str(root / "build/dependency-bin/Debug") + os.pathsep + str(root / "build/dependency-bin") + os.pathsep + env["PATH"]
                    result = self.run_command([executable, "bad"], root, success=False, env=env)
                    self.assertIn("AddressSanitizer: heap-buffer-overflow", result.stdout + result.stderr)

    @unittest.skipIf(os.name == "nt", "PIC behavior is a portable-platform check")
    def test_pic_default_and_explicit_parent_override(self):
        for supplied, expected in ((None, "ON"), ("OFF", "OFF"), ("ON", "ON")):
            with self.subTest(pic=supplied), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.write_project(root, pic=supplied)
                self.configure(root)
                self.assertEqual((root / "build/pic.txt").read_text(), expected)


if __name__ == "__main__":
    unittest.main()
