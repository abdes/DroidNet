"""Validated clangd adjustments and compilation database contexts."""

from __future__ import annotations

import ctypes
import hashlib
import json
import os
import re
import shlex
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from .common import ToolError, absolute, digest, path_key, validate, yaml_documents

SOURCES = {".c", ".cc", ".cpp", ".cxx"}
HEADERS = {".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".ipp", ".tpp"}
STRING_LIST = {
    "oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]
}
FLAGS_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "properties": {
        "CompilationDatabase": {"type": "string"},
        "Add": STRING_LIST,
        "Remove": STRING_LIST,
        "Compiler": {"type": "string", "minLength": 1},
    },
}
DATABASE_SCHEMA = {
    "type": "array",
    "minItems": 1,
    "items": {
        "type": "object",
        "required": ["directory", "file"],
        "anyOf": [{"required": ["arguments"]}, {"required": ["command"]}],
        "properties": {
            "directory": {"type": "string"},
            "file": {"type": "string"},
            "output": {"type": "string"},
            "command": {"type": "string", "minLength": 1},
            "arguments": {"type": "array", "minItems": 1, "items": {"type": "string"}},
        },
    },
}

# Deliberately bounded operand grammar. Unknown exact Remove rules fail validation.
OPERANDS = {
    "-I": ("-I", "--include-directory"),
    "-isystem": ("-isystem",),
    "-iquote": ("-iquote",),
    "-idirafter": ("-idirafter",),
    "-include": ("-include",),
    "-imacros": ("-imacros",),
    "-D": ("-D",),
    "-U": ("-U",),
    "-o": ("-o",),
    "-MF": ("-MF",),
    "-MT": ("-MT",),
    "-MQ": ("-MQ",),
    "-x": ("-x",),
    "-target": ("-target", "--target"),
    "-isysroot": ("-isysroot",),
    "--sysroot": ("--sysroot",),
    "-resource-dir": ("-resource-dir",),
    "-mllvm": ("-mllvm",),
    "/I": ("/I",),
    "/D": ("/D",),
    "/U": ("/U",),
    "/FI": ("/FI",),
    "/Fo": ("/Fo",),
    "/Fd": ("/Fd",),
    "/external:I": ("/external:I",),
}
TOGGLES = {"-c", "/c", "/MP", "-MD", "-MDd", "/MD", "/MDd", "-MMD", "-MP"}
JOINED = ("-std=", "-std:", "/std:", "-W", "-f", "-m", "/Zc:", "-D", "-U")


@dataclass(frozen=True)
class ClangdConfig:
    database: Path | None
    add: tuple[str, ...]
    remove: tuple[str, ...]
    compiler: str | None = None


@dataclass(frozen=True)
class Context:
    file: Path
    directory: Path
    arguments: tuple[str, ...]
    output: str
    response_inputs: tuple[tuple[str, str], ...] = ()

    @property
    def identity(self) -> str:
        return digest([path_key(self.file), path_key(self.directory), self.arguments])[
            :24
        ]

    def entry(
        self, extra_before: list[str] | None = None, extra: list[str] | None = None
    ) -> dict:
        return {
            "directory": str(self.directory),
            "file": str(self.file),
            "output": self.output,
            "arguments": [
                self.arguments[0],
                *(extra_before or []),
                *self.arguments[1:],
                *(extra or []),
            ],
        }


def string_list(value: str | list[str]) -> tuple[str, ...]:
    return (value,) if isinstance(value, str) else tuple(value)


def parse_clangd(path: Path, build_override: Path | None = None) -> ClangdConfig:
    if not path.is_file():
        raise ToolError(
            f"Missing clangd configuration: {path}\n"
            "This checkout requires a configured Ninja build tree.\n"
            "CMake generates .clangd from the tracked .clangd.in template.\n"
            "Existing configuration? Select it with --clangd-file PATH.\n"
            "oxytidy does not configure or build."
        )
    blocks = []
    for document in yaml_documents(path.read_text(encoding="utf-8-sig")):
        if document is None:
            continue
        validate(document, {"type": "object"}, str(path))
        if "CompileFlags" not in document:
            continue
        if "If" in document:
            raise ToolError(
                "Conditional CompileFlags are unsupported; provide an unconditional --clangd-file"
            )
        validate(document["CompileFlags"], FLAGS_SCHEMA, str(path))
        blocks.append(document["CompileFlags"])
    if len(blocks) != 1:
        raise ToolError("Expected exactly one unconditional CompileFlags block")
    block = blocks[0]
    remove = string_list(block.get("Remove", []))
    for rule in remove:
        if not rule or "*" in rule[:-1]:
            raise ToolError(f"Unsupported Remove pattern: {rule!r}")
        if (
            not rule.endswith("*")
            and rule not in OPERANDS
            and rule not in TOGGLES
            and not rule.startswith(JOINED)
        ):
            raise ToolError(
                f"Unsupported Remove flag: {rule}; supported operand flags: {', '.join(OPERANDS)}"
            )
    db = block.get("CompilationDatabase")
    if build_override is None and (not db or db in {"Ancestors", "None"}):
        raise ToolError(
            "Set CompileFlags.CompilationDatabase to a directory or pass --build-dir"
        )
    return ClangdConfig(
        build_override or absolute(db, path.parent),
        string_list(block.get("Add", [])),
        remove,
        block.get("Compiler"),
    )


def split_command(command: str, windows: bool | None = None) -> list[str]:
    if windows is None:
        windows = os.name == "nt"
    if not windows:
        return shlex.split(command)
    shell = ctypes.windll.shell32
    kernel = ctypes.windll.kernel32
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    count = ctypes.c_int()
    argv = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not argv:
        raise ToolError("Cannot tokenize compilation command")
    try:
        return [argv[i] for i in range(count.value)]
    finally:
        kernel.LocalFree(argv)


def expand_responses(
    args: list[str],
    directory: Path,
    active: tuple[Path, ...] = (),
    *,
    inputs: dict[str, str] | None = None,
) -> list[str]:
    expanded = []
    for arg in args:
        if not arg.startswith("@"):
            expanded.append(arg)
            continue
        path = absolute(arg[1:], directory)
        if path in active or len(active) >= 32:
            raise ToolError(f"Recursive response file: {path}")
        raw = path.read_bytes()
        if inputs is not None:
            key, content_hash = path_key(path), hashlib.sha256(raw).hexdigest()
            if key in inputs and inputs[key] != content_hash:
                raise ToolError(
                    f"Response file changed during command preparation: {path}"
                )
            inputs[key] = content_hash
        text = raw.decode(
            "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        )
        # Prefix a dummy argv[0]: Windows gives the executable different quoting rules.
        words = split_command("compiler " + text)[1:]
        expanded.extend(
            expand_responses(words, directory, (*active, path), inputs=inputs)
        )
    return expanded


def remove_arguments(args: list[str], rules: tuple[str, ...]) -> list[str]:
    result = [args[0]]
    i = 1
    while i < len(args):
        wrapped = args[i] == "-Xclang" and i + 1 < len(args)
        index = i + 1 if wrapped else i
        arg = args[index]
        canonical = None
        separate = False
        for name, aliases in OPERANDS.items():
            for alias in aliases:
                if arg == alias:
                    canonical, separate = name, True
                    break
                if arg.startswith(alias + "=") or (
                    alias
                    in {
                        "-I",
                        "-D",
                        "-U",
                        "/I",
                        "/D",
                        "/U",
                        "/FI",
                        "/Fo",
                        "/Fd",
                        "/external:I",
                    }
                    and arg.startswith(alias)
                    and len(arg) > len(alias)
                ):
                    canonical = name
                    break
            if canonical:
                break
        matched = any(
            (rule.endswith("*") and arg.startswith(rule[:-1]))
            or rule == arg
            or rule == canonical
            for rule in rules
        )
        if matched and canonical is None and arg not in TOGGLES:
            joined = "=" in arg or arg.startswith(
                ("-std:", "/std:", "/Zc:", "-W", "-D", "-U")
            )
            machine_switch = re.fullmatch(
                r"-m(no-)?(32|64|avx|avx2|sse|sse2|sse3|ssse3|sse4|sse4[.]1|sse4[.]2)",
                arg,
            )
            if not joined and not machine_switch:
                raise ToolError(
                    f"Cannot safely remove {arg}: its operand grammar is unsupported"
                )
        end = index + 1
        if separate:
            if wrapped and end < len(args) and args[end] == "-Xclang":
                end += 1
            if end >= len(args):
                raise ToolError(f"Missing operand for {arg}")
            end += 1
        if not matched:
            result.extend(args[i:end])
        i = end
    return result


def configuration_name(entry: dict, args: list[str]) -> str | None:
    for arg in args:
        match = re.match(r"[-/]DCMAKE_INTDIR=(.*)", arg)
        if match:
            return match[1].strip('\\"')
    output = entry.get("output", "").replace("\\", "/")
    for part in output.split("/"):
        if part in {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"}:
            return part
    return None


def read_database(
    path: Path,
    config: ClangdConfig,
    configuration: str | None,
    *,
    source_filter: Callable[[Path], bool] | None = None,
) -> list[Context]:
    if not path.is_file():
        raise ToolError(
            f"Missing compilation database: {path}\n"
            "Configure this checkout with CMAKE_EXPORT_COMPILE_COMMANDS=ON "
            "and a supported generator, or select its existing database "
            "directory explicitly with --build-dir PATH. Build any required "
            "generated headers before analysis."
        )
    entries = json.loads(path.read_text(encoding="utf-8-sig"))
    validate(entries, DATABASE_SCHEMA, str(path))
    contexts = {}
    unknown = []
    scoped_entries = 0
    for entry in entries:
        directory = absolute(entry["directory"], path.parent)
        source = absolute(entry["file"], directory)
        if source.suffix.lower() not in SOURCES:
            continue
        if source_filter is not None and not source_filter(source):
            continue
        scoped_entries += 1
        response_inputs: dict[str, str] = {}
        args = expand_responses(
            entry.get("arguments") or split_command(entry["command"]),
            directory,
            inputs=response_inputs,
        )
        name = configuration_name(entry, args)
        if configuration and name is None:
            unknown.append(str(source))
            continue
        if configuration and name != configuration:
            continue
        args = remove_arguments(args, config.remove)
        args.extend(
            expand_responses(list(config.add), directory, inputs=response_inputs)
        )
        if config.compiler:
            args[0] = config.compiler
        context = Context(
            source,
            directory,
            tuple(args),
            entry.get("output", ""),
            tuple(sorted(response_inputs.items())),
        )
        contexts[context.identity] = context
    if unknown:
        raise ToolError(
            f"Cannot establish configuration {configuration!r} for {len(unknown)} entries (first: {unknown[0]}). Use --configuration all to analyze all recorded contexts."
        )
    if not contexts and (source_filter is None or scoped_entries):
        raise ToolError("No compilation contexts match the requested configuration")
    return sorted(
        contexts.values(),
        key=lambda context: (path_key(context.file), context.identity),
    )
