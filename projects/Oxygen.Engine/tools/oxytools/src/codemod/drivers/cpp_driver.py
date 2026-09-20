"""Semantic rename planning across recorded C++ compilation contexts."""

from __future__ import annotations

import ctypes
import os
from dataclasses import dataclass
from pathlib import Path

from oxytools.common import ToolError, fingerprint, unchanged
from oxytools.compilation import HEADERS, SOURCES, read_database, remove_arguments

from ..lexical import byte_span, spans
from ..patcher import apply_edits


def load_clang(library: str | None = None):
    import clang.cindex as clang

    if library:
        file = Path(library).resolve()
        if not file.is_file():
            raise ToolError(f"Missing libclang library: {file}")
        if clang.Config.loaded and Path(clang.conf.get_filename()).resolve() != file:
            raise ToolError(
                "A different libclang is already loaded; start a new process to select another library"
            )
    if not clang.Config.loaded:
        if library:
            clang.Config.set_library_file(str(file))
        elif os.name == "nt" and not clang.Config.library_file:
            file = (
                Path(os.environ.get("ProgramFiles", "C:/Program Files"))
                / "LLVM/bin/libclang.dll"
            )
            if file.is_file():
                clang.Config.set_library_file(str(file))
    try:
        return clang, clang.Index.create()
    except clang.LibclangError as error:
        raise ToolError(
            f"Cannot load a compatible libclang; use --libclang-file PATH. {error}"
        ) from error


@dataclass
class Unit:
    context: object
    arguments: list[str]


@dataclass(frozen=True)
class Declaration:
    identity: str
    parent: str
    name: str
    spelling: str
    path: Path
    line: int
    column: int
    matches_kind: bool
    context: str
    external: bool


class CppDriver:
    SUPPORTED_EXTENSIONS = SOURCES | HEADERS

    def __init__(self, context):
        self.ctx = context
        self.clang, self.index = load_clang(context.args.libclang_file)
        self.units: list[Unit] = []
        self.declarations: dict[str, list[Declaration]] = {}
        self.new_declarations: list[Declaration] = []
        self.override_edges: dict[str, set[str]] = {}
        self.observations: dict[tuple[Path, int], set[str]] = {}

    @staticmethod
    def _qualified(cursor):
        parts = []
        while (
            cursor is not None
            and not cursor.kind.is_invalid()
            and cursor.kind.name != "TRANSLATION_UNIT"
        ):
            if cursor.spelling:
                parts.append(cursor.spelling)
            cursor = cursor.semantic_parent
        return "::".join(reversed(parts))

    def _identity(self, cursor):
        if cursor is None or cursor.kind.is_invalid():
            return ""
        if cursor.kind.name in {"CONSTRUCTOR", "DESTRUCTOR"}:
            cursor = cursor.semantic_parent
        specialized = cursor.specialized_template
        if specialized is not None and not specialized.kind.is_invalid():
            cursor = specialized
        canonical = cursor.canonical
        identity = canonical.get_usr()
        # Clang's file-local USRs include only a basename. Distinct same.cpp
        # files must not turn unrelated static/local declarations into one target.
        if (
            identity
            and canonical.location.file
            and canonical.linkage != self.clang.LinkageKind.EXTERNAL
        ):
            origin = os.path.normcase(str(Path(canonical.location.file.name).resolve()))
            return origin + "::" + identity
        return identity

    @staticmethod
    def _walk(cursor):
        stack = [cursor]
        while stack:
            current = stack.pop()
            yield current
            stack.extend(reversed(list(current.get_children())))

    def _parse(self, file, arguments, *, modified=None):
        unsaved = [(str(path), data) for path, data in self.ctx.sources.data.items()]
        if modified:
            unsaved = [(name, modified.get(Path(name), data)) for name, data in unsaved]
        try:
            translation = self.index.parse(None, args=arguments, unsaved_files=unsaved)
        except self.clang.TranslationUnitLoadError as error:
            raise ToolError(
                f"C++ parsing failed for {file}; compile flags were not discarded"
            ) from error
        errors = [
            str(diagnostic)
            for diagnostic in translation.diagnostics
            if diagnostic.severity >= self.clang.Diagnostic.Error
        ]
        if errors:
            raise ToolError(f"C++ parsing failed for {file}:\n" + "\n".join(errors))
        return translation

    def _kind_matches(self, cursor):
        if not cursor.kind.is_declaration() or cursor.kind.name == "USING_DECLARATION":
            return False
        kinds = {
            "class": {
                "CLASS_DECL",
                "STRUCT_DECL",
                "CLASS_TEMPLATE",
                "CLASS_TEMPLATE_PARTIAL_SPECIALIZATION",
            },
            "function": {"FUNCTION_DECL", "CXX_METHOD", "FUNCTION_TEMPLATE"},
            "variable": {"VAR_DECL", "PARM_DECL", "FIELD_DECL", "ENUM_CONSTANT_DECL"},
            "member": {"FIELD_DECL", "CXX_METHOD"},
            "namespace": {"NAMESPACE"},
        }
        return (
            self.ctx.args.kind is None or cursor.kind.name in kinds[self.ctx.args.kind]
        )

    def _overridden(self, cursor):
        clang = self.clang
        pointers = ctypes.POINTER(clang.Cursor)()
        size = ctypes.c_uint()
        get = clang.conf.lib.clang_getOverriddenCursors
        get.argtypes = [
            clang.Cursor,
            ctypes.POINTER(ctypes.POINTER(clang.Cursor)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        get.restype = None
        dispose = clang.conf.lib.clang_disposeOverriddenCursors
        dispose.argtypes = [ctypes.POINTER(clang.Cursor)]
        dispose.restype = None
        get(cursor, ctypes.byref(pointers), ctypes.byref(size))
        try:
            for i in range(size.value):
                parent = pointers[i]
                parent._tu = cursor.translation_unit
                yield self._identity(parent)
        finally:
            dispose(pointers)

    def _collect(self, translation, editable, old, context):
        usages = {
            "DECL_REF_EXPR",
            "MEMBER_REF_EXPR",
            "TYPE_REF",
            "TEMPLATE_REF",
            "NAMESPACE_REF",
            "OVERLOADED_DECL_REF",
        }
        for cursor in self._walk(translation.cursor):
            if not cursor.location.file or cursor.kind.name == "USING_DECLARATION":
                continue
            is_declaration = cursor.kind.is_declaration()
            if is_declaration and cursor.spelling.lstrip("~") in {
                old,
                self.ctx.args.to_sym,
            }:
                identity = self._identity(cursor)
                if identity:
                    record = Declaration(
                        identity,
                        self._identity(cursor.semantic_parent),
                        self._qualified(cursor),
                        cursor.spelling,
                        Path(cursor.location.file.name).resolve(),
                        cursor.location.line,
                        cursor.location.column,
                        self._kind_matches(cursor),
                        context,
                        cursor.linkage == self.clang.LinkageKind.EXTERNAL,
                    )
                    if cursor.spelling == self.ctx.args.to_sym:
                        self.new_declarations.append(record)
                    else:
                        self.declarations.setdefault(identity, []).append(record)
                        if (
                            cursor.kind.name == "CXX_METHOD"
                            and cursor.is_virtual_method()
                        ):
                            for parent in self._overridden(cursor):
                                self.override_edges.setdefault(identity, set()).add(
                                    parent
                                )
                                self.override_edges.setdefault(parent, set()).add(
                                    identity
                                )
            if not (is_declaration or cursor.kind.name in usages):
                continue
            path = Path(cursor.location.file.name).resolve()
            if path not in editable:
                continue
            offset = cursor.location.offset
            data = self.ctx.sources.read(path)
            if cursor.kind.name == "DESTRUCTOR" and data[offset : offset + 1] == b"~":
                offset += 1
            if data[offset : offset + len(old.encode("utf-8"))] != old.encode("utf-8"):
                continue
            if cursor.kind.name == "OVERLOADED_DECL_REF":
                references = set()
                for index in range(
                    self.clang.conf.lib.clang_getNumOverloadedDecls(cursor)
                ):
                    reference = self.clang.conf.lib.clang_getOverloadedDecl(
                        cursor, index
                    )
                    reference._tu = cursor.translation_unit
                    references.add(self._identity(reference))
            else:
                reference = cursor if is_declaration else cursor.referenced
                references = {self._identity(reference)}
            self.observations.setdefault((path, offset), set()).update(references)

    def generate_edits(self, files, translation_files):
        args = self.ctx.args
        old = args.from_sym.rsplit("::", 1)[-1]
        if not old.isidentifier() or not args.to_sym.isidentifier():
            raise ToolError(
                "C++ renames require an identifier as --to and an identifier or qualified name as --from; moving declarations is unsupported"
            )
        root = self.ctx.project_info.root_dir
        editable = set(files)
        inputs = fingerprint([self.ctx.project_info.configuration_root / ".clangd"])
        config = self.ctx.project_info.compile_config(args.build_dir)
        database = config.database / "compile_commands.json"
        inputs.update(fingerprint([database]))
        contexts = read_database(
            database,
            config,
            None if args.configuration == "all" else args.configuration,
            source_filter=lambda path: path in translation_files,
        )
        if not contexts:
            raise ToolError("No recorded C++ compilation contexts in the selected root")
        missing = [
            str(path)
            for path in files
            if path.suffix.lower() in SOURCES
            and path not in {context.file for context in contexts}
        ]
        if missing:
            raise ToolError("No matching compile command: " + ", ".join(missing))
        if not unchanged(inputs):
            raise ToolError("Compilation configuration changed during preparation")
        reached = set()
        for context in contexts:
            if self.ctx.trace:
                self.ctx.trace(f"Analyze {context.file}", False)
                self.ctx.trace(repr(context.entry()), True)
            for name, expected in context.response_inputs:
                if name in inputs and inputs[name] != expected:
                    raise ToolError(
                        f"Response file changed between compilation contexts: {name}"
                    )
                inputs[name] = expected
            command = remove_arguments(
                list(context.arguments),
                ("-c", "/c", "-o", "-MF", "-MT", "-MQ", "/Fo", "/Fd"),
            )
            compiler = Path(command.pop(0)).name.lower()
            if compiler in {"cl", "cl.exe", "clang-cl", "clang-cl.exe"}:
                command.insert(0, "--driver-mode=cl")
            command.insert(0, f"-working-directory={context.directory}")
            self.ctx.sources.read(context.file)
            translation = self._parse(context.file, command)
            reached.add(context.file)
            for inclusion in translation.get_includes():
                path = Path(inclusion.include.name).resolve()
                reached.add(path)
                if path.is_relative_to(root):
                    self.ctx.sources.read(path)
            # Analyze the captured project bytes, including discovered headers.
            translation = self._parse(context.file, command)
            self._collect(translation, editable, old, context.identity)
            self.units.append(Unit(context, command))
            del translation
        missing = editable - reached
        if missing:
            raise ToolError(
                "No recorded translation unit reaches: "
                + ", ".join(map(str, sorted(missing)))
            )

        targets = {}
        declarations = self.declarations
        override_edges = self.override_edges
        at = None
        if args.at:
            try:
                name, line, column = args.at.rsplit(":", 2)
                at = ((root / name).resolve(), int(line), int(column))
                if at[1] < 1 or at[2] < 1:
                    raise ValueError
            except ValueError as error:
                raise ToolError(
                    "--at must be FILE:LINE:COLUMN with positive one-based positions"
                ) from error
        for identity, records in declarations.items():
            for record in records:
                if record.path not in editable or not record.matches_kind:
                    continue
                if "::" in args.from_sym and record.name != args.from_sym.lstrip(":"):
                    continue
                if at and (
                    record.path != at[0]
                    or record.line != at[1]
                    or not record.column
                    <= at[2]
                    < record.column + len(record.spelling.encode("utf-8"))
                ):
                    continue
                targets[identity] = record
        if not targets:
            raise ToolError(
                "No C++ declaration matches the requested name, kind, and location"
            )
        if len(targets) != 1:
            choices = sorted(
                {
                    f"{record.name} at {record.path}:{record.line}:{record.column}"
                    for record in targets.values()
                }
            )
            raise ToolError(
                "Ambiguous C++ name; select a declaration with --at FILE:LINE:COLUMN:\n"
                + "\n".join(choices)
            )
        identities = set(targets)
        while True:
            expanded = identities | {
                parent
                for identity in identities
                for parent in override_edges.get(identity, ())
            }
            if expanded == identities:
                break
            identities = expanded
        for identity in identities:
            if identity not in declarations:
                raise ToolError(
                    "Virtual-method rename requires a base declaration outside the analyzed scope"
                )
            for record in declarations[identity]:
                if record.path not in editable:
                    raise ToolError(
                        f"Rename requires a declaration outside the edit scope: {record.path}:{record.line}"
                    )

        target_declarations = [
            record for identity in identities for record in declarations[identity]
        ]
        for record in self.new_declarations:
            if any(
                record.parent == target.parent
                and (
                    record.context == target.context
                    or (record.external and target.external)
                )
                for target in target_declarations
            ):
                raise ToolError(
                    f"The new name already exists in a declaration scope: {record.path}:{record.line}"
                )
        observations = self.observations
        safe = {}
        for key, references in observations.items():
            if references & identities:
                if references - identities:
                    raise ToolError(
                        f"Compilation contexts or overloaded references disagree on symbol identity: {key[0]}:{key[1]}"
                    )
                path, offset = key
                safe[key] = self.ctx.sources.edit(
                    path, offset, offset + len(old.encode("utf-8")), args.to_sym
                )
        if not safe:
            raise ToolError(
                "The selected declaration has no directly editable source tokens"
            )
        edits = list(safe.values())
        modified = {}
        for path in {edit.file_path for edit in edits}:
            modified[path] = apply_edits(
                self.ctx.sources.read(path),
                [edit for edit in edits if edit.file_path == path],
            )
        for unit in self.units:
            self._parse(unit.context.file, unit.arguments, modified=modified)
        self.ctx.sources.verify()
        if not unchanged(inputs):
            raise ToolError("Compilation configuration changed during rename")
        review = []
        for path in files:
            text = self.ctx.sources.read(path).decode("utf-8")
            for start, end in spans(text, old):
                start, end = byte_span(text, start, end)
                if (path, start) not in observations:
                    review.append(
                        self.ctx.sources.edit(
                            path,
                            start,
                            end,
                            args.to_sym,
                            "C++ token unresolved in the selected compilation contexts",
                        )
                    )
        return edits, review
