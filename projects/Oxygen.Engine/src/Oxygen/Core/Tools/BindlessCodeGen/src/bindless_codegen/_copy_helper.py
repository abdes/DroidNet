#!/usr/bin/env python3
"""
Unified copy helper for Bindless generation.

Features: - Copy a single generated file into the source tree with intelligent
behavior
    (skip when only timestamp differs).
- Batch mode: process all files under a generated temp dir.
- Clear, machine-friendly log lines are printed so CMake logs show what
    happened per-file: COPIED_NEW, COPIED, SKIPPED_TIMESTAMP, UNCHANGED, FAILED.

Usage:
    python -m bindless_codegen._copy_helper <src_or_generated_dir> <dst_or_dest_root> [--verbose]

Behavior:
    - If the first positional argument is a file, the script treats
        it as a single-file copy and applies timestamp-only diff skipping. The
        destination may be a file path or an existing directory; when a
        directory is supplied the source basename is used.
    - If the first positional argument is a directory, the script
        treats it as a generated output directory and batch-processes all files
        under it, recreating the relative layout under the destination root
        (which must be a directory).

Exit codes: 0 success, non-zero on error or if any individual copy failed.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
import shutil
import re
from typing import Iterable, List
import difflib


TIMESTAMP_RE = re.compile(r"\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}")


def read_lines(path: Path) -> List[str]:
    try:
        return path.read_text(encoding="utf-8").splitlines(keepends=True)
    except Exception:
        return path.read_text(encoding="latin-1").splitlines(keepends=True)


def vprint(msg: str, verbose: bool) -> None:
    if verbose:
        print(msg)


def copy_single(src: Path, dst: Path, verbose: bool) -> int:
    """Copy a single file with timestamp-only-diff handling.

    Returns 0 on success, non-zero on error.
    Prints a single log line describing the result.
    """
    if not src.exists():
        vprint(f"Failed: missing source: {src.name}", verbose)
        return 3

    if not dst.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            shutil.copy2(src, dst)
            vprint(f"Copied (new): {dst.name}", verbose)
            return 0
        except Exception as e:
            vprint(f"Failed: {dst.name} ({e})", verbose)
            return 4

    # Perform a single-pass difflib.SequenceMatcher on raw lines and inspect
    # opcode slices: if files are identical -> Unchanged; if all differing
    # opcode slices are identical after stripping timestamp-like substrings ->
    # Skip; otherwise treat as real change and copy.
    src_lines = read_lines(src)
    dst_lines = read_lines(dst)

    # Use difflib.SequenceMatcher on raw lines as the primary comparison.
    sm = difflib.SequenceMatcher(a=src_lines, b=dst_lines, autojunk=False)

    # No diffs
    if sm.ratio() == 1.0:
        vprint(f"Unchanged: {dst.name}", verbose)
        return 0

    # Check opcodes: ensure all non-equal slices match after stripping timestamps
    timestamp_only = True
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        a_slice = "".join(src_lines[i1:i2])
        b_slice = "".join(dst_lines[j1:j2])
        a_stripped = TIMESTAMP_RE.sub("", a_slice)
        b_stripped = TIMESTAMP_RE.sub("", b_slice)
        if a_stripped != b_stripped:
            timestamp_only = False
            break

    if timestamp_only:
        vprint(f"Skipping (timestamp diff only): {dst.name}", verbose)
        return 0

    # Real change; copy
    try:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        vprint(f"Copied: {dst.name}", verbose)
        return 0
    except Exception as e:
        vprint(f"Failed: {dst.name} ({e})", verbose)
        return 4


def iter_generated_files(generated_dir: Path) -> Iterable[Path]:
    if not generated_dir.exists():
        return []
    return (p for p in sorted(generated_dir.rglob("*")) if p.is_file())


def run_batch(generated_dir: Path, dest_root: Path, verbose: bool) -> int:
    # Ensure dest_root is a directory (or create it).
    if dest_root.exists() and dest_root.is_file():
        vprint(
            f"Failed: destination for batch must be a directory: {dest_root}",
            verbose,
        )
        return 3
    dest_root.mkdir(parents=True, exist_ok=True)

    files: List[Path] = list(iter_generated_files(generated_dir))

    if not files:
        vprint("No files: nothing to do", verbose)
        return 0

    any_failed = False
    for p in files:
        if generated_dir and generated_dir in p.parents:
            rel = p.relative_to(generated_dir)
            rel_display = str(rel)
        else:
            rel = p.name
            rel_display = p.name
        dst = dest_root / rel
        rc = copy_single(p, dst, verbose)
        if rc != 0:
            any_failed = True

    return 1 if any_failed else 0


def main(argv: List[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog="bindless_copy_helper")
    parser.add_argument(
        "src_or_generated_dir",
        type=Path,
        help="Source file or generated directory",
    )
    parser.add_argument(
        "dst_or_dest_root",
        type=Path,
        help="Destination file or destination root",
    )
    parser.add_argument("--verbose", action="store_true")

    args = parser.parse_args(argv)

    src = args.src_or_generated_dir
    dst = args.dst_or_dest_root

    # Auto-detect mode: file -> single copy, directory -> batch
    if src.is_file():
        # dst may be a directory or a file path. If it's a directory, copy into it with the
        # same basename as src.
        if dst.exists() and dst.is_dir():
            dst_file = dst / src.name
        else:
            dst_file = dst
        return copy_single(src, dst_file, args.verbose)
    else:
        # treat src as generated dir for batch mode
        if dst.exists() and dst.is_file():
            print(f"FAILED: destination for batch must be a directory: {dst}")
            return 3
        dst.mkdir(parents=True, exist_ok=True)
        return run_batch(src, dst, args.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
