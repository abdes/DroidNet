# pyright: strict
"""Shared fixtures for the Oxygen CMake and Conan tooling tests.

Run the suite from an x64 VS 2026 developer shell. ``OXYGEN_TEST_CMAKE`` selects
an exact CMake executable; otherwise the one on ``PATH`` is used.
"""

import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from collections.abc import Callable, Iterable, Mapping, Sequence
from pathlib import Path
from types import ModuleType
from typing import Any

type Argument = str | os.PathLike[str]
type Command = Sequence[Argument]
type Result = subprocess.CompletedProcess[str]
type Json = dict[str, Any]
"""A decoded JSON object; its values are only known at run time."""
type GraphNodes = dict[str, Json]
"""Conan graph nodes by id; node ``"0"`` is the root recipe."""

ENGINE = Path(__file__).resolve().parents[3]
CMAKE_MODULES = ENGINE / "cmake"
PROFILES = ENGINE / "profiles"
WINDOWS_PROFILE = PROFILES / "windows-msvc.ini"
WINDOWS = os.name == "nt"
EXE_SUFFIX = ".exe" if WINDOWS else ""


def find_tool(name: str, override: str | None = None) -> str:
    """The tool's path, from the ``override`` environment variable or ``PATH``; empty when absent."""
    return (os.environ.get(override) if override else None) or shutil.which(name) or ""


CMAKE = find_tool("cmake", "OXYGEN_TEST_CMAKE")
CTEST = str(Path(CMAKE).with_name(f"ctest{EXE_SUFFIX}")) if CMAKE else ""
CONAN = find_tool("conan")
CLANGD = find_tool("clangd")
GIT = find_tool("git")
NATIVE_COMPILER = find_tool("cl" if WINDOWS else "c++")

NINJA = "Ninja"
NINJA_MULTI = "Ninja Multi-Config"
VISUAL_STUDIO = "Visual Studio 18 2026"
NATIVE_GENERATORS = (NINJA, NINJA_MULTI, *((VISUAL_STUDIO,) if WINDOWS else ()))

COMMAND_TIMEOUT = 1800.0


def skip_unless[T: Callable[..., object]](condition: object, reason: str) -> Callable[[T], T]:
    """A typed ``unittest.skipUnless`` for test classes and methods."""

    def decorate(item: T) -> T:
        return unittest.skipUnless(condition, reason)(item)

    return decorate


def requires_cmake[T: Callable[..., object]](item: T) -> T:
    return skip_unless(CMAKE, "CMake is required")(item)


def requires_conan[T: Callable[..., object]](item: T) -> T:
    return skip_unless(CONAN, "Conan is required")(item)


def requires_compiler[T: Callable[..., object]](item: T) -> T:
    return skip_unless(CMAKE and NATIVE_COMPILER, "CMake and a native compiler (x64 developer shell) are required")(
        item
    )


def windows_only[T: Callable[..., object]](item: T) -> T:
    return skip_unless(WINDOWS, "Windows-only")(item)


def requires_env[T: Callable[..., object]](variable: str, purpose: str) -> Callable[[T], T]:
    """Skip unless ``variable`` is set: for tests that need provisioned external state."""
    return skip_unless(os.environ.get(variable), f"{purpose}: set {variable}=1")


def include(module: str) -> str:
    """A CMake ``include()`` line for one of the engine's CMake modules."""
    return f'include("{(CMAKE_MODULES / module).as_posix()}")\n'


def cmake_project(name: str, languages: str = "NONE", body: str = "") -> str:
    """A top-level CMakeLists.txt at the engine's CMake minimum."""
    return f"cmake_minimum_required(VERSION 4.2)\nproject({name} {languages})\n{body}"


def oxygen_recipe(
    *,
    attributes: Mapping[str, str] | None = None,
    stubs: Iterable[str] = ("requirements", "build_requirements"),
) -> str:
    """A conanfile.py deriving from the engine recipe with selected hooks stubbed.

    ``attributes`` maps class attribute names to Python expressions.
    """
    lines = [
        "import importlib.util",
        f"spec = importlib.util.spec_from_file_location('oxygen_recipe', {str(ENGINE / 'conanfile.py')!r})",
        "module = importlib.util.module_from_spec(spec)",
        "spec.loader.exec_module(module)",
        "class Fixture(module.OxygenConan):",
        *(f"    {name} = {value}" for name, value in (attributes or {}).items()),
        *(f"    def {hook}(self): pass" for hook in stubs),
    ]
    return "\n".join(lines) + "\n"


def write(path: Path, text: str = "// fixture\n") -> Path:
    """Write UTF-8 text, creating parent directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def read_json(path: Path) -> Any:
    return json.loads(read(path))


def mtimes(paths: Iterable[Path]) -> dict[Path, int]:
    return {path: path.stat().st_mtime_ns for path in paths}


def output(result: Result) -> str:
    return result.stdout + result.stderr


def msbuild_items(path: Path, kind: str) -> list[ET.Element]:
    """Elements of one MSBuild item kind (ClInclude, Filter, ...) in a project file."""
    return [element for element in ET.parse(path).iter() if element.tag.rpartition("}")[2] == kind]


def load_module(name: str, path: Path) -> ModuleType:
    """Import a Python file that is not on ``sys.path``."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def options(*values: str) -> list[str]:
    """Conan ``-o`` arguments for each value."""
    return [argument for value in values for argument in ("-o", value)]


class CommandTestCase(unittest.TestCase):
    """Runs tools with captured output and keeps per-test files in a temp root."""

    temp_prefix = "oxygen tooling "
    root: Path

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix=self.temp_prefix)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def run_command(
        self,
        command: Command,
        *,
        cwd: Path | None = None,
        success: bool = True,
        env: Mapping[str, str] | None = None,
        timeout: float = COMMAND_TIMEOUT,
    ) -> Result:
        result = subprocess.run(
            [os.fspath(part) for part in command],
            cwd=cwd or self.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            timeout=timeout,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, output(result))
        else:
            self.assertNotEqual(result.returncode, 0, output(result))
        return result

    def assert_fails_with(self, command: Command, message: str) -> None:
        self.assertIn(message, output(self.run_command(command, success=False)))

    def configure(
        self, source: Path, build: Path, generator: str = NINJA, *arguments: str, success: bool = True
    ) -> Result:
        return self.run_command([CMAKE, "-S", source, "-B", build, "-G", generator, *arguments], success=success)

    def build(self, build: Path, *arguments: str, config: str = "Debug", success: bool = True) -> Result:
        return self.run_command([CMAKE, "--build", build, "--config", config, *arguments], success=success)

    def cmake_script(self, text: str, *arguments: Argument, name: str = "script.cmake", success: bool = True) -> Result:
        script = write(self.root / name, "cmake_minimum_required(VERSION 4.2)\n" + text)
        return self.run_command([CMAKE, "-P", script, *arguments], success=success)

    def ctest(self, build: Path, test: str, config: str = "Debug") -> None:
        result = self.run_command(
            [CTEST, "--test-dir", build, "-C", config, "-R", f"^{test}$", "--no-tests=error", "-V"]
        )
        self.assertIn("100% tests passed", result.stdout)


class ProjectTestCase(CommandTestCase):
    """A generated CMake project in ``source``, configured into ``binary``."""

    source: Path
    binary: Path

    def setUp(self) -> None:
        super().setUp()
        self.source = self.root / "source"
        self.binary = self.root / "build"

    def write(self, relative: str, text: str = "// fixture\n") -> Path:
        return write(self.source / relative, text)

    def project(
        self,
        body: str,
        *,
        language: str = "NONE",
        modules: Iterable[str] = ("BuildHelpers.cmake", "LogHelpers.cmake"),
    ) -> None:
        self.write("CMakeLists.txt", cmake_project("Fixture", language, "".join(map(include, modules)) + body))

    def configure_project(self, generator: str = NINJA, *arguments: str, success: bool = True) -> Result:
        return self.configure(
            self.source, self.binary, generator, "-DCMAKE_BUILD_TYPE=Debug", *arguments, success=success
        )


class ConanGraphTestCase(CommandTestCase):
    """Reads the engine recipe's resolved graph from the local Conan cache."""

    def graph(self, *arguments: str, profile: Path = WINDOWS_PROFILE) -> GraphNodes:
        result = self.run_command(
            [
                CONAN,
                "graph",
                "info",
                ENGINE,
                "-pr:h",
                profile,
                "-pr:b",
                WINDOWS_PROFILE,
                "-s:h",
                "build_type=Debug",
                "-s:b",
                "build_type=Release",
                "--no-remote",
                "--format=json",
                *arguments,
            ],
            cwd=ENGINE,
        )
        nodes: GraphNodes = json.loads(result.stdout)["graph"]["nodes"]
        return nodes
