"""Validate a complete replacement batch before coordinated, atomic file writes."""

from __future__ import annotations

import os
import stat
import tempfile
from pathlib import Path

from .common import ToolError, file_hash, path_key
from .scope import Scope


def plan_fixes(
    records: list[dict], scope: Scope, snapshot: dict[str, str]
) -> tuple[dict[str, list[dict]], list[dict]]:
    edits: dict[str, list[dict]] = {}
    skipped = []
    for record in records:
        sets = record.get("replacement_sets", [record["replacements"]])
        nonempty = [group for group in sets if group]
        if not nonempty:
            skipped.append(
                {"diagnostic": record["id"], "reason": "No automatic replacement"}
            )
            continue
        if any(group != nonempty[0] for group in sets):
            raise ToolError(
                f"Compilation contexts propose different fixes for {record['file']}:{record['line']}"
            )
        group = nonempty[0]
        if any(not scope.contains(Path(edit["file"])) for edit in group):
            skipped.append(
                {
                    "diagnostic": record["id"],
                    "reason": "Replacement set crosses the project edit scope",
                }
            )
            continue
        for edit in group:
            key = path_key(edit["file"])
            if key not in snapshot:
                raise ToolError(
                    f"No analyzed-content snapshot for replacement target: {edit['file']}"
                )
            edits.setdefault(key, [])
            if edit not in edits[key]:
                edits[key].append(edit)
    for path, group in edits.items():
        group.sort(key=lambda edit: (edit["offset"], edit["length"], edit["text"]))
        previous = None
        for edit in group:
            if previous and (
                edit["offset"] < previous["offset"] + previous["length"]
                or edit["offset"] == previous["offset"]
            ):
                raise ToolError(
                    f"Conflicting replacements in {path} at byte {edit['offset']}"
                )
            previous = edit
    return edits, skipped


def replaced_bytes(
    original: bytes, edits: list[dict]
) -> tuple[bytes, list[tuple[int, int]]]:
    if original.startswith((b"\xff\xfe", b"\xfe\xff")):
        raise ToolError("Autofix requires UTF-8 source; UTF-16 files are report-only")
    try:
        original.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ToolError(
            "Autofix requires UTF-8 source; the original encoding will not be rewritten"
        ) from error
    newline = "\r\n" if b"\r\n" in original else "\n"
    chunks = []
    ranges = []
    cursor = written = 0
    for edit in edits:
        offset, length = edit["offset"], edit["length"]
        if offset + length > len(original) or offset < cursor:
            raise ToolError("Replacement byte range is outside the analyzed contents")
        if original.startswith(b"\xef\xbb\xbf") and offset < 3:
            raise ToolError("Replacement would modify the UTF-8 BOM")
        prefix = original[cursor:offset]
        replacement = (
            edit["text"].replace("\r\n", "\n").replace("\n", newline).encode("utf-8")
        )
        chunks.extend([prefix, replacement])
        written += len(prefix)
        ranges.append((written, max(1, len(replacement))))
        written += len(replacement)
        cursor = offset + length
    chunks.append(original[cursor:])
    return b"".join(chunks), ranges


def atomic_write(path: Path, content: bytes, mode: int) -> None:
    descriptor, name = tempfile.mkstemp(prefix=".oxytidy-", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def apply_fixes(
    edits: dict[str, list[dict]],
    snapshot: dict[str, str],
    formatter=None,
    cancelled=None,
) -> list[str]:
    prepared = {}
    originals = {}
    modes = {}
    for name, group in edits.items():
        path = Path(name)
        if file_hash(path) != snapshot[name]:
            raise ToolError(f"Contents changed since analysis: {path}")
        original = path.read_bytes()
        content, ranges = replaced_bytes(original, group)
        if formatter:
            content = formatter(path, content, ranges)
        originals[name], prepared[name] = original, content
        modes[name] = stat.S_IMODE(path.stat().st_mode)
    # Validate the entire batch again after preparing/formatting every file.
    for name in prepared:
        if file_hash(name) != snapshot[name]:
            raise ToolError(f"Contents changed while preparing fixes: {name}")
    applied = []
    try:
        for name, content in prepared.items():
            if cancelled and cancelled():
                raise ToolError("Cancelled before completing fix application")
            if file_hash(name) != snapshot[name]:
                raise ToolError(f"Contents changed before applying fixes: {name}")
            atomic_write(Path(name), content, modes[name])
            applied.append(name)
    except Exception as error:
        unrestored = []
        for name in reversed(applied):
            try:
                if Path(name).read_bytes() != prepared[name]:
                    unrestored.append(name)
                    continue
                atomic_write(Path(name), originals[name], modes[name])
            except OSError:
                unrestored.append(name)
        if unrestored:
            raise ToolError(
                f"Fix application failed; concurrent changes or I/O errors prevented rollback of {unrestored}: {error}"
            ) from error
        raise
    return applied
