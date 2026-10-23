# pyright: strict
"""Build/run checks for private policy and sanitizer compile/link/runtime behavior."""

import json
import os
import sys
import unittest
from pathlib import Path

from cmake_testing import (
    CMAKE,
    NATIVE_GENERATORS,
    NINJA,
    NINJA_MULTI,
    WINDOWS,
    CommandTestCase,
    Result,
    cmake_project,
    include,
    read,
    requires_compiler,
    windows_only,
    write,
)

WARNING_FLAG = "/W4" if WINDOWS else "-Wextra"

SUBJECT = """#include <cstdlib>
#if defined(_CPPRTTI) || defined(__GXX_RTTI)
#error Oxygen must disable RTTI
#endif
extern "C" int exercise(int offset) {
  volatile int* p = static_cast<int*>(std::malloc(4 * sizeof(int)));
  p[0] = 42; int value = p[offset]; std::free(const_cast<int*>(p)); return value;
}
"""

CONSUMER = """#include <cstring>
extern "C" int exercise(int);
struct A { virtual ~A() = default; }; struct B : A {};
int main(int argc, char** argv) { B b; A* a = &b;
  if (!dynamic_cast<B*>(a)) return 2;
  return exercise(argc > 1 && !std::strcmp(argv[1], "bad") ? 12 : 0) == 42 ? 0 : 1;
}
"""


@requires_compiler
class CompilerPolicyTests(CommandTestCase):
    def setUp(self) -> None:
        super().setUp()
        self.binary = self.root / "build"

    def write_project(
        self, *, asan: bool = False, shared: bool = False, strict: bool = False, pic: str | None = None
    ) -> None:
        oxygen = self.root / "oxygen"
        write(
            oxygen / "CMakeLists.txt",
            "set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)\n"
            f"add_library(subject {'SHARED' if shared else 'STATIC'} subject.cpp warning.c)\n"
            "set_target_properties(subject PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON\n"
            '  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dependency-bin")\n'
            + ("set_property(TARGET subject PROPERTY COMPILE_WARNING_AS_ERROR ON)\n" if strict else ""),
        )
        write(oxygen / "subject.cpp", SUBJECT)
        write(oxygen / "warning.c", "int warning_probe(void) { int intentionally_unused; return 0; }\n")
        write(self.root / "main.cpp", CONSUMER)
        write(
            self.root / "CMakeLists.txt",
            cmake_project(
                "Policy",
                "C CXX",
                "enable_testing()\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
                f"set(OXYGEN_WITH_ASAN {'ON' if asan else 'OFF'})\n"
                + (f"set(CMAKE_POSITION_INDEPENDENT_CODE {pic})\n" if pic is not None else "")
                + include("CompilerPolicy.cmake")
                + 'if(MSVC)\nstring(APPEND CMAKE_EXE_LINKER_FLAGS " /OPT:NOREF")\nendif()\n'
                "add_subdirectory(oxygen)\n"
                'oxygen_apply_directory_build_policy("${CMAKE_CURRENT_SOURCE_DIR}/oxygen")\n'
                "add_library(GTest::gtest INTERFACE IMPORTED)\n"
                + include("GTestHelpers.cmake")
                + "gtest_program(consumer SOURCES main.cpp DEPS subject)\n"
                'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/consumer-$<CONFIG>.txt" CONTENT "$<TARGET_FILE:consumer>")\n'
                "get_target_property(_interface subject INTERFACE_COMPILE_OPTIONS)\n"
                'if(_interface)\nmessage(FATAL_ERROR "Private policy leaked: ${_interface}")\nendif()\n'
                "get_target_property(_pic subject POSITION_INDEPENDENT_CODE)\n"
                'file(WRITE "${CMAKE_BINARY_DIR}/pic.txt" "${_pic}")\n',
            ),
        )

    def configure_policy(self, generator: str = NINJA) -> Result:
        return self.configure(self.root, self.binary, generator, "-DCMAKE_BUILD_TYPE=Debug")

    def compile_command(self, source: str) -> str:
        commands = json.loads(read(self.binary / "compile_commands.json"))
        return next(entry["command"] for entry in commands if entry["file"].endswith(source))

    def consumer(self) -> str:
        return read(self.binary / "consumer-Debug.txt")

    def test_private_warnings_rtti_and_module_error_opt_in(self) -> None:
        for strict in (False, True):
            with self.subTest(strict=strict):
                self.binary = self.root / f"build-{strict}"
                self.write_project(strict=strict)
                self.configure_policy()
                result = self.build(self.binary, "--verbose", success=not strict)
                self.assertIn("intentionally_unused", result.stdout + result.stderr)
                if not strict:
                    self.run_command([self.consumer()])
                    self.assertIn(WARNING_FLAG, self.compile_command("subject.cpp"))
                    self.assertNotIn(WARNING_FLAG, self.compile_command("main.cpp"))

    @windows_only
    def test_execution_with_multiple_transitive_runtime_directories(self) -> None:
        self.write_project(shared=True)
        oxygen = self.root / "oxygen"
        with (oxygen / "CMakeLists.txt").open("a", encoding="utf-8") as stream:
            stream.write(
                "add_library(second SHARED second.cpp)\n"
                "set_target_properties(second PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON\n"
                '  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/second dependency")\n'
                "target_link_libraries(subject PRIVATE second)\n"
            )
        write(oxygen / "second.cpp", 'extern "C" int second_value() { return 42; }\n')
        write(
            oxygen / "subject.cpp",
            SUBJECT.replace(
                'extern "C" int exercise', 'extern "C" int second_value();\nextern "C" int exercise'
            ).replace("p[0] = 42;", "p[0] = second_value();"),
        )
        self.configure_policy(NINJA_MULTI)
        self.build(self.binary)
        self.ctest(self.binary, "consumer")

    @windows_only
    def test_ccache_targets_embed_debug_info_and_link_in_full(self) -> None:
        # A pass-through launcher named as the ccache tool stands in for ccache.
        self.write_project(shared=True)
        launcher = f"{Path(CMAKE).as_posix()};-E;env"
        self.configure(
            self.root,
            self.binary,
            NINJA,
            "-DCMAKE_BUILD_TYPE=Debug",
            f"-DCCACHE_TOOL_PATH={Path(CMAKE).as_posix()}",
            f"-DCMAKE_C_COMPILER_LAUNCHER={launcher}",
            f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}",
        )
        result = self.build(self.binary, "--verbose")
        self.assertIn("Z7", self.compile_command("subject.cpp"))
        link = next(line for line in result.stdout.splitlines() if "subject.dll" in line and "/dll" in line.lower())
        self.assertIn("/INCREMENTAL:NO", link)

    def test_runtime_launcher_preserves_arguments_and_exit_status(self) -> None:
        arguments = ["", "a;b", '"quoted"', "]==]", "trailing\\", "${not_expanded}"]
        result = self.cmake_script(
            f"set(OXYGEN_RUNTIME_COMMAND [==[{Path(CMAKE).as_posix()};-E;env;--]==])\n" + include("RunTest.cmake"),
            "--",
            sys.executable,
            "-c",
            "import json,sys; print(json.dumps(sys.argv[1:])); sys.exit(37)",
            *arguments,
            name="launcher.cmake",
            success=False,
        )
        self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout), arguments)

    def test_static_and_shared_asan_detection_and_test_execution(self) -> None:
        for generator in NATIVE_GENERATORS:
            for shared in (False, True):
                with self.subTest(generator=generator, shared=shared):
                    self.binary = self.root / f"build-{generator}-{shared}"
                    self.write_project(asan=True, shared=shared)
                    self.check_asan(generator)

    def check_asan(self, generator: str) -> None:
        self.configure_policy(generator)
        result = self.build(self.binary, "--verbose")
        self.assertIn("fsanitize=address", result.stdout)
        if WINDOWS and generator.startswith(NINJA):
            self.assertIn("/OPT:NOREF", result.stdout)
            self.assertIn("/INCREMENTAL:NO", result.stdout)
            subject = self.compile_command("subject.cpp")
            self.assertNotIn("/RTC", subject)
            self.assertNotIn("/ZI", subject)
        self.ctest(self.binary, "consumer")
        env = dict(os.environ)
        if WINDOWS:
            runtime = self.binary / "dependency-bin"
            env["PATH"] = os.pathsep.join((str(runtime / "Debug"), str(runtime), env["PATH"]))
        result = self.run_command([self.consumer(), "bad"], success=False, env=env)
        self.assertIn("AddressSanitizer: heap-buffer-overflow", result.stdout + result.stderr)

    @unittest.skipIf(WINDOWS, "PIC behavior is a portable-platform check")
    def test_pic_default_and_explicit_parent_override(self) -> None:
        for supplied, expected in ((None, "ON"), ("OFF", "OFF"), ("ON", "ON")):
            with self.subTest(pic=supplied):
                self.binary = self.root / f"build-{supplied}"
                self.write_project(pic=supplied)
                self.configure_policy()
                self.assertEqual(read(self.binary / "pic.txt"), expected)


if __name__ == "__main__":
    unittest.main()
