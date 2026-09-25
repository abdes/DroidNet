#!/usr/bin/env python3
"""Check Vortex documentation links, migration coverage and captured evidence."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote


ENGINE = Path(__file__).resolve().parents[2]
ROOT = ENGINE / "design/vortex"
REPO = ENGINE.parents[1]
LINK = re.compile(r"\]\(([^)\n]+)\)")
SECTION_LABELS = {"Validation:", "Recorded evidence:", "Evidence:", "Evidence so far:", "Runtime closure command shape:"}


def slug(text: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    return re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")


def headings(text: str) -> set[str]:
    result: set[str] = set()
    counts: Counter[str] = Counter()
    fenced = False
    for line in text.splitlines():
        if line.startswith("```"):
            fenced = not fenced
        if fenced:
            continue
        match = re.match(r"^#+\s+(.+)", line)
        if match:
            key = slug(match[1])
            count = counts[key]
            counts[key] += 1
            result.add(key + (f"-{count}" if count else ""))
    return result


def inline_html_anchors(text: str) -> list[int]:
    """Find rendered HTML anchors, excluding fenced and inline code."""
    result = []
    fence = ""
    for number, line in enumerate(text.splitlines(), 1):
        marker = re.match(r"^\s*(`{3,}|~{3,})", line)
        if marker:
            if not fence:
                fence = marker[1]
            elif marker[1][0] == fence[0] and len(marker[1]) >= len(fence):
                fence = ""
            continue
        if not fence and re.search(r"<a\b", re.sub(r"(`+).*?\1", "", line), re.I):
            result.append(number)
    return result


def normalized(text: str) -> str:
    text = LINK.sub("]", text)
    text = re.sub(r"^#+[^\n]*", "", text, flags=re.M)
    text = re.sub(r"^\|[ :|\-]+\|\s*$", "", text, flags=re.M)
    text = text.replace("**", "").replace("\\|", "|")
    return re.sub(r"\s+", "", text)


def status_index(documents: dict[Path, str]) -> tuple[str, list[str]]:
    """Derive the progress view from the permanent milestone records."""
    rows = []
    errors = []
    for path, text in sorted(documents.items()):
        if path.name != "README.md" or "milestones" not in path.relative_to(ROOT).parts:
            continue
        opening = "\n".join(text.splitlines()[:18])
        state = re.search(r"^Status: `([a-z_]+)`", opening, re.M)
        fields = {name: re.search(r"^\| " + name + r"\s*\| (.+?)\s*\|$", opening, re.M)
                  for name in ("Outcome", "Remaining", "Evidence")}
        if not state or not all(fields.values()):
            errors.append(f"Missing leading status block: {path.relative_to(ROOT)}")
            continue
        remaining = fields["Remaining"][1]

        def rebase(match: re.Match[str]) -> str:
            target, separator, anchor = match[1].partition("#")
            resolved = (path.parent / target).resolve() if target else path
            relative = Path(os.path.relpath(resolved, ROOT)).as_posix()
            return "](" + relative + ("#" + anchor if separator else "") + ")"

        remaining = LINK.sub(rebase, remaining)
        label = "Exposure package" if path.parent.name == "exposure" else path.parent.name
        rows.append(f"| [{label}]({path.relative_to(ROOT).as_posix()}) | `{state[1]}` | {remaining} |")
    text = """# Vortex milestone status

Generated from the leading status blocks in the milestone READMEs. Edit the
owning record, then run `python tools/vortex/CheckDocumentation.py --write-status`
from the engine directory. [Open items](OPEN_ITEMS.md) owns unfinished work.

| Milestone | Status | Remaining work / scope extensions |
| --- | --- | --- |
""" + "\n".join(rows) + "\n"
    return text, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--migration", action="store_true", help="Also compare retained content with the pre-refactor baseline")
    parser.add_argument("--write-status", action="store_true", help="Regenerate STATUS.md from milestone status blocks")
    parser.add_argument("--legacy", type=Path, help="Original vortex copy; defaults to the recorded Git revision")
    parser.add_argument("--report", type=Path, help="Write the complete JSON result")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "archive/migration-map.json").read_text(encoding="utf-8"))
    errors: list[str] = []
    unavailable: set[str] = set()
    documents = {p: p.read_text(encoding="utf-8-sig") for p in ROOT.rglob("*.md") if "evidence" not in p.relative_to(ROOT).parts}
    progress, progress_errors = status_index(documents)
    errors.extend(progress_errors)
    open_work = documents.get(ROOT / "OPEN_ITEMS.md", "")
    open_rows = []
    priority = ""
    for line in open_work.splitlines():
        heading = re.match(r'^## (.+)$', line)
        if heading:
            priority = slug(heading[1])
        row = re.match(r'^\|\s*((?:VX-[A-Z]+-\d+|EV01-[A-Z-]+))\s*\|\s*`([a-z_]+)`\s*\|', line)
        if row:
            open_rows.append((priority, row[1], row[2]))
    open_ids = {row[1] for row in open_rows}
    if not open_rows or len(open_ids) != len(open_rows):
        errors.append("OPEN_ITEMS.md must contain table rows with unique work-item IDs")
    priorities = {"p1--current-delivery", "p2--engineering-follow-ups", "p3--unscheduled-capabilities"}
    for priority, item_id, state in open_rows:
        if priority not in priorities or state not in {"incomplete", "pending", "deferred", "decision", "verification"}:
            errors.append(f"Invalid open-item priority or state: {item_id}")
    for priority in priorities:
        summary = re.search(r'^\|\s*\[[^\]]+\]\(#' + re.escape(priority) + r'\)\s*\|[^|]+\|\s*(\d+)\s*\|', open_work, re.M)
        if not summary or int(summary[1]) != sum(row[0] == priority for row in open_rows):
            errors.append(f"Open-item summary count does not match its priority table: {priority}")
    boundary_text = documents.get(ROOT / "milestones/ED-M08/deferred-capabilities.md", "")
    for item_id in re.findall(r"^### (EV01-[A-Z-]+)", boundary_text, re.M):
        if item_id not in open_ids:
            errors.append(f"Deferred capability missing from OPEN_ITEMS.md: {item_id}")
    progress_path = ROOT / "STATUS.md"
    if args.write_status:
        progress_path.write_text(progress, encoding="utf-8", newline="\n")
        documents[progress_path] = progress
    elif normalized(documents.get(progress_path, "")) != normalized(progress):
        errors.append("STATUS.md is stale; run CheckDocumentation.py --write-status")
    tracked = set(subprocess.check_output(["git", "ls-files", "-z"], cwd=REPO).decode().split("\0"))
    incoming = {}
    for name in tracked:
        path = REPO / name
        if path.suffix.lower() != ".md" or path.is_relative_to(ROOT) or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8-sig")
        local_links = []
        for match in LINK.finditer(text):
            href = match[1]
            if re.match(r"^[a-zA-Z][\w+.-]*:", href):
                continue
            target = (path.parent / href.partition("#")[0]).resolve()
            if target.is_relative_to(ROOT):
                local_links.append(match[0])
        if local_links:
            incoming[path] = "\n".join(local_links)
    heading_cache = {p.resolve(): headings(t) for p, t in documents.items()}
    graph: dict[Path, set[Path]] = {p.resolve(): set() for p in documents}
    for folder in (ROOT / "milestones").rglob("*"):
        if not folder.is_dir() or "evidence" in folder.relative_to(ROOT).parts:
            continue
        if folder.name in {"EX051", "EX052", "EX081", "EX082"} or re.match(r"^(?:EX\d+|VTX-M\d+[A-Z])_\d", folder.name):
            errors.append(f"Use a dot-delimited subdivision folder: {folder.relative_to(ROOT)}")
    checked_links = 0
    for path, text in (documents | incoming).items():
        label = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path.relative_to(REPO)
        if path in documents:
            for line in inline_html_anchors(text):
                errors.append(f"Inline HTML anchor: {label}:{line}; use a Markdown heading")
        for match in LINK.finditer(text):
            href = unquote(match[1]).strip("<>")
            if re.match(r"^[a-zA-Z][\w+.-]*:", href) or href.startswith("//"):
                continue
            link, _, fragment = href.partition("#")
            target = (path.parent / link).resolve() if link else path.resolve()
            checked_links += 1
            if "out" in target.parts:
                if not target.exists():
                    unavailable.add(str(target))
                continue
            if not target.exists():
                errors.append(f"Link: {label} -> {href}")
                continue
            if target in graph and path.resolve() in graph:
                graph[path.resolve()].add(target)
            if fragment and target.suffix.lower() == ".md":
                if target not in heading_cache:
                    heading_cache[target] = headings(target.read_text(encoding="utf-8-sig"))
                if fragment not in heading_cache[target]:
                    errors.append(f"Anchor: {label} -> {href}")

    reached: set[Path] = set()
    pending = [(ROOT / "README.md").resolve()]
    while pending:
        path = pending.pop()
        if path in reached:
            continue
        reached.add(path)
        pending.extend(graph.get(path, set()) - reached)
    for path in documents:
        if path.name == "README.md" and "milestones" in path.parts:
            if path.resolve() not in reached:
                errors.append(f"Unreachable milestone: {path.relative_to(ROOT)}")
            if not re.search(r"(?:Status:|\*\*Status:\*\*)\s*`[a-z_]+`", documents[path], re.I):
                errors.append(f"Missing milestone status: {path.relative_to(ROOT)}")

    artifact_count = 0
    for item in manifest["artifacts"]:
        path = ROOT / item["destination"]
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            errors.append(f"Evidence bytes: {item['destination']}")
        if path.relative_to(REPO).as_posix() not in tracked:
            errors.append(f"Untracked evidence: {item['destination']}")
        artifact_count += 1

    checked_sections = 0
    if args.migration:
        old_sources = {}
        for name, digest in manifest["sources"].items():
            if args.legacy:
                data = (args.legacy / name).read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    errors.append(f"Legacy source hash: {name}")
            else:
                data = subprocess.check_output(["git", "show", f"{manifest['baseline']}:projects/Oxygen.Engine/design/vortex/{name}"], cwd=REPO)
            old_sources[name] = data.decode("utf-8-sig").splitlines(keepends=True)
        covered = {name: Counter() for name in old_sources}
        for item in manifest["sections"]:
            name, first, last = item["source"], item["first"], item["last"]
            covered[name].update(range(first, last + 1))
            destination = ROOT / item["destination"]
            destinations = [item["destination"], *item.get("additional_destinations", [])]
            if not destination.exists():
                errors.append(f"Coverage destination: {item['destination']}")
                continue
            if item["disposition"] == "consolidated":
                if not item.get("reason"):
                    errors.append(f"Missing consolidation rationale: {name}:{first}")
                continue
            source = "".join(old_sources[name][first - 1:last])
            for old, new in sorted(manifest["destinations"].items(), key=lambda pair: -len(pair[0])):
                source = source.replace("design/vortex/" + old, "design/vortex/" + new)
                source = source.replace("design\\vortex\\" + old.replace("/", "\\"), "design\\vortex\\" + new.replace("/", "\\"))
            applied_raw_edits = set()
            for edit in manifest.get("editorial_replacements", []):
                if edit["destination"] in destinations:
                    previous_source = source
                    if edit["before"] in SECTION_LABELS:
                        source = re.sub(r"^[ \t]*" + re.escape(edit["before"]) + r"[ \t]*$", edit["after"], source, flags=re.M)
                    else:
                        source = source.replace(edit["before"], edit["after"])
                    if source != previous_source:
                        applied_raw_edits.add(id(edit))
            destination_text = "\n".join((ROOT / name).read_text(encoding="utf-8-sig") for name in destinations)
            actual = normalized(destination_text) + normalized("\n".join(
                re.findall(r"^#+\s+(.+)$", destination_text, flags=re.M)))
            missing = []
            for paragraph in re.split(r"\n\s*\n", source):
                if paragraph.lstrip().startswith("|"):
                    candidates = [cell for line in paragraph.splitlines() for cell in re.split(r"(?<!\\)\|", line.strip().strip("|")) if not re.fullmatch(r"[\s:\-]*", cell)]
                elif paragraph.startswith("Status:") and "\n" in paragraph:
                    # The status now has a leading table; retain the historical
                    # date/result paragraph independently of its old adjacency.
                    candidates = paragraph.split("\n", 1)
                else:
                    candidates = [paragraph]
                for candidate in candidates:
                    value = normalized(candidate)
                    for edit in manifest.get("editorial_replacements", []):
                        if edit["destination"] in destinations and id(edit) not in applied_raw_edits:
                            before = normalized(edit["before"])
                            if before:
                                if edit["before"] not in SECTION_LABELS or value == before:
                                    value = value.replace(before, normalized(edit["after"]))
                    reviewed = False
                    if value and value not in actual:
                        # A source-backed rewrite can replace a whole section.
                        # Require both its recorded rationale and every replacement
                        # paragraph, rather than accepting a bare deletion waiver.
                        for edit in manifest.get("editorial_replacements", []):
                            if edit["destination"] not in destinations or not edit.get("reason"):
                                continue
                            if value not in normalized(edit["before"]):
                                continue
                            replacement = [normalized(p) for p in re.split(r"\n\s*\n", edit["after"])]
                            replacement = [p for p in replacement if p]
                            if replacement and all(p in actual for p in replacement):
                                reviewed = True
                                break
                    if value and value not in actual and not reviewed:
                        missing.append(candidate.strip()[:160])
            if missing:
                errors.append(f"Content: {name}:{first}-{last} -> {item['destination']}: {missing[:4]}")
            checked_sections += 1
        for name, lines in old_sources.items():
            gaps = [n for n in range(1, len(lines) + 1) if covered[name][n] != 1]
            if gaps:
                errors.append(f"Source range coverage: {name}: {gaps[:10]}")

    report = {"documents": len(documents), "links": checked_links, "retained_sections": checked_sections,
              "open_items": len(open_rows), "milestone_status_records": len(re.findall(r'^\| \[', progress, re.M)),
              "evidence_files": artifact_count, "unavailable_local_run_artifacts": sorted(unavailable), "errors": errors}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    for error in errors[:60]:
        print(error)
    coverage = f"{checked_sections} retained sections; " if args.migration else ""
    print(f"{len(documents)} documents; {checked_links} links; {coverage}{artifact_count} evidence hashes; {len(errors)} errors.")
    print(f"{len(unavailable)} historical local run artifacts unavailable; no runtime checks repeated.")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
