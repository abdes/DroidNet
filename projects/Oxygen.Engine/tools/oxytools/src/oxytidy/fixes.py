"""Validate a complete replacement batch before coordinated, atomic file writes."""

from __future__ import annotations

import hashlib
import re
import stat
from itertools import groupby
from pathlib import Path

from oxytools.common import ToolError, file_hash, path_key
from oxytools.files import atomic_write
from oxytools.includes import prepare_includes

from .scope import Scope

_INCLUDE_BLOCK = re.compile(
    r'(?:[ \t]*#[ \t]*include[ \t]+(?:<[^<>\r\n]+>|"[^"\r\n]+")[ \t]*\r?\n)+'
)


def _is_include_deletion(edit: dict, original: bytes, starts: set[int]) -> bool:
    offset = edit["offset"]
    end = offset + edit["length"]
    if edit["text"] or end > len(original):
        return False
    deleted = original[offset:end]
    if not _INCLUDE_BLOCK.fullmatch(deleted.decode("utf-8")):
        return False
    for line in deleted.splitlines(keepends=True):
        directive = offset + len(line) - len(line.lstrip(b" \t"))
        if directive not in starts:
            return False
        offset += len(line)
    return True


def _merge_include_edits(group: list[dict], original: bytes) -> list[dict]:
    merged = []
    include_starts = None
    for _, at_offset in groupby(group, key=lambda edit: edit["offset"]):
        candidates = list(at_offset)
        insertions = [edit for edit in candidates if edit["length"] == 0]
        deletions = [edit for edit in candidates if edit["length"] > 0]
        compatible = (
            len(candidates) > 1
            and bool(insertions)
            and len(deletions) <= 1
            and all(_INCLUDE_BLOCK.fullmatch(edit["text"]) for edit in insertions)
        )
        if compatible and deletions:
            if include_starts is None:
                _, ranges = prepare_includes(original)
                include_starts = {offset for offset, _ in ranges}
            compatible = _is_include_deletion(deletions[0], original, include_starts)
        if compatible:
            lines = sorted(
                {
                    line
                    for edit in insertions
                    for line in edit["text"].replace("\r\n", "\n").split("\n")[:-1]
                }
            )
            merged.append(
                {
                    **insertions[0],
                    "length": deletions[0]["length"] if deletions else 0,
                    "text": "\n".join(lines) + "\n",
                }
            )
        else:
            merged.extend(candidates)
    return merged


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
            path = Path(record["file"]).resolve()
            raise ToolError(
                f"Compilation contexts propose different fixes for {path}:{record['line']}"
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
            if edit["length"] == 0 and _INCLUDE_BLOCK.fullmatch(edit["text"]):
                normalized, _ = prepare_includes(edit["text"].encode("utf-8"))
                edit = {**edit, "text": normalized.decode("utf-8")}
            key = path_key(edit["file"])
            if key not in snapshot:
                raise ToolError(
                    f"No analyzed-content snapshot for replacement target: {Path(edit['file']).resolve()}"
                )
            edits.setdefault(key, [])
            if edit not in edits[key]:
                edits[key].append(edit)
    for key, group in edits.items():
        path = Path(group[0]["file"]).resolve()
        original = path.read_bytes()
        if hashlib.sha256(original).hexdigest() != snapshot[key]:
            raise ToolError(f"Contents changed since analysis: {path}")
        group.sort(key=lambda edit: (edit["offset"], edit["length"], edit["text"]))
        group = _merge_include_edits(group, original)
        edits[key] = group
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


def apply_fixes(
    edits: dict[str, list[dict]],
    snapshot: dict[str, str],
    formatter=None,
    cancelled=None,
) -> list[str]:
    prepared = {}
    originals = {}
    modes = {}
    paths = {}
    for name, group in edits.items():
        path = Path(name).resolve()
        if file_hash(path) != snapshot[name]:
            raise ToolError(f"Contents changed since analysis: {path}")
        original = path.read_bytes()
        content, ranges = replaced_bytes(original, group)
        if formatter:
            content = formatter(path, content, ranges)
        if content == original:
            continue
        originals[name], prepared[name] = original, content
        modes[name] = stat.S_IMODE(path.stat().st_mode)
        paths[name] = path
    # Validate the entire batch again after preparing/formatting every file.
    for name in prepared:
        if file_hash(paths[name]) != snapshot[name]:
            raise ToolError(f"Contents changed while preparing fixes: {paths[name]}")
    applied = []
    try:
        for name, content in prepared.items():
            if cancelled and cancelled():
                raise ToolError("Cancelled before completing fix application")
            if file_hash(paths[name]) != snapshot[name]:
                raise ToolError(
                    f"Contents changed before applying fixes: {paths[name]}"
                )
            atomic_write(paths[name], content, modes[name])
            applied.append(name)
    except Exception as error:
        unrestored = []
        for name in reversed(applied):
            try:
                if paths[name].read_bytes() != prepared[name]:
                    unrestored.append(str(paths[name]))
                    continue
                atomic_write(paths[name], originals[name], modes[name])
            except OSError:
                unrestored.append(str(paths[name]))
        if unrestored:
            raise ToolError(
                f"Fix application failed; concurrent changes or I/O errors prevented rollback of {unrestored}: {error}"
            ) from error
        raise
    return applied
