"""Validated byte edits and coordinated, non-overwriting Git patch output."""

from __future__ import annotations

import difflib
import stat
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

from oxytools.common import ToolError
from oxytools.files import atomic_write


@dataclass(frozen=True)
class Edit:
    file_path: Path
    offset: int
    original_text: str
    replacement_text: str
    reason: str = ""

    @property
    def end(self) -> int:
        return self.offset + len(self.original_text.encode("utf-8"))


class Sources:
    def __init__(self, paths=()) -> None:
        self.data: dict[Path, bytes] = {}
        for path in paths:
            self.read(path)

    def read(self, path: Path) -> bytes:
        path = Path(path).resolve()
        if path not in self.data:
            data = path.read_bytes()
            try:
                data.decode("utf-8-sig")
            except UnicodeDecodeError as error:
                raise ToolError(f"UTF-8 source required: {path}") from error
            self.data[path] = data
        return self.data[path]

    def verify(self) -> None:
        for path, original in self.data.items():
            if path.read_bytes() != original:
                raise ToolError(f"Source changed during rename: {path}")

    def location(self, edit: Edit) -> tuple[int, int]:
        prefix = self.read(edit.file_path)[: edit.offset]
        return prefix.count(b"\n") + 1, len(prefix.rsplit(b"\n", 1)[-1]) + 1

    def edit(
        self, path: Path, start: int, end: int, replacement: str, reason: str = ""
    ) -> Edit:
        data = self.read(path)
        if start < 0 or end <= start or end > len(data):
            raise ToolError(f"Invalid edit range in {path}: {start}:{end}")
        return Edit(
            Path(path).resolve(),
            start,
            data[start:end].decode("utf-8"),
            replacement,
            reason,
        )


def apply_edits(original: bytes, edits: list[Edit]) -> bytes:
    distinct = {}
    for edit in edits:
        key = (edit.offset, edit.original_text, edit.replacement_text)
        distinct[key] = edit
    ordered = sorted(distinct.values(), key=lambda edit: edit.offset)
    end = -1
    for edit in ordered:
        if not edit.original_text or edit.offset < 0 or edit.end > len(original):
            raise ToolError(
                f"Invalid replacement range: {edit.file_path}:{edit.offset}"
            )
        if edit.offset < end:
            raise ToolError(f"Conflicting replacements: {edit.file_path}:{edit.offset}")
        if original[edit.offset : edit.end] != edit.original_text.encode("utf-8"):
            raise ToolError(
                f"Replacement does not match source: {edit.file_path}:{edit.offset}"
            )
        end = edit.end
    for edit in reversed(ordered):
        original = (
            original[: edit.offset]
            + edit.replacement_text.encode("utf-8")
            + original[edit.end :]
        )
    return original


def _git_path(name: str) -> str:
    if all(33 <= byte < 127 and byte not in (34, 92) for byte in name.encode("utf-8")):
        return name
    encoded = ""
    for byte in name.encode("utf-8"):
        if byte in (34, 92):
            encoded += "\\" + chr(byte)
        elif byte < 32 or byte >= 127:
            encoded += f"\\{byte:03o}"
        else:
            encoded += chr(byte)
    return '"' + encoded + '"'


class PatchGenerator:
    def __init__(self, root: Path, sources: Sources) -> None:
        self.root = root.resolve()
        self.sources = sources

    def render(self, edits: list[Edit], *, base_edits: list[Edit] = ()) -> bytes:
        groups = defaultdict(list)
        for edit in edits:
            path = edit.file_path.resolve()
            if not path.is_relative_to(self.root) or path not in self.sources.data:
                raise ToolError(
                    f"Replacement is outside the captured source scope: {path}"
                )
            groups[path].append(edit)
        result = []
        for path in sorted(groups):
            original = self.sources.read(path)
            base = [edit for edit in base_edits if edit.file_path.resolve() == path]
            modified = apply_edits(original, base + groups[path])
            original = apply_edits(original, base)
            name = path.relative_to(self.root).as_posix()
            for line in difflib.unified_diff(
                original.decode("utf-8").splitlines(keepends=True),
                modified.decode("utf-8").splitlines(keepends=True),
                fromfile=_git_path("a/" + name),
                tofile=_git_path("b/" + name),
            ):
                result.append(
                    line
                    if line.endswith("\n")
                    else line + "\n\\ No newline at end of file\n"
                )
        return "".join(result).encode("utf-8")

    def write(self, outputs: list[tuple[Path, bytes]]) -> None:
        paths = [path.resolve() for path, _ in outputs]
        if len(set(paths)) != len(paths):
            raise ToolError("Safe and review patches must have different output paths")
        if any(path.exists() or path.is_symlink() for path in paths):
            raise ToolError(
                "Refusing to overwrite an existing patch output; choose a new path"
            )
        self.sources.verify()
        created: dict[Path, bytes] = {}
        try:
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb"):
                    pass
                created[path] = b""
            for path, (_, content) in zip(paths, outputs):
                atomic_write(
                    path, content, stat.S_IMODE(path.stat().st_mode), expected=b""
                )
                created[path] = content
            self.sources.verify()
        except (OSError, ToolError, KeyboardInterrupt) as error:
            retained = []
            for path, content in created.items():
                try:
                    if path.read_bytes() == content:
                        path.unlink()
                    else:
                        retained.append(str(path))
                except OSError:
                    retained.append(str(path))
            if retained:
                raise ToolError(
                    f"Patch output failed; could not remove concurrently changed artifacts {retained}: {error}"
                ) from error
            raise
