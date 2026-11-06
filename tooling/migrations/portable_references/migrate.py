"""Rewrites an editor project's authored data to the portable-references contract.

One-time and outside the product: no product code reads the old forms. The rules work on an
in-memory tree, a mapping from project-relative paths to text, so each is tested without a disk;
the command line loads a project into one, writes what changed and reports the rest.

Usage: python -m tooling.migrations.portable_references.migrate --root <folder of projects> [--apply]
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, cast
from urllib.parse import unquote

JsonObject = dict[str, Any]

CURRENT_PROJECT_SCHEMA = 3
MOUNTS_SCHEMA = 2
ASSET_URI = "asset:///"
OUTPUT_PREFIX = ".cooked/"
RECORD_SUFFIX = ".import.json"
DESCRIPTOR_SUFFIXES = (
    ".otex.json",
    ".omat.json",
    ".ogeo.json",
    ".oscene.json",
    ".opscene.json",
    ".ocshape.json",
    ".opmat.json",
)
TREE_SUFFIXES = (".json", ".oxy")
SKIPPED_FOLDERS = {
    ".cooked",
    ".build",
    ".git",
    ".vs",
    ".vscode",
    ".scratch",
    "Types",
    "Config",
}
STALE_BUILD_FOLDERS = (".build/cache/cooked-dependencies-v3",)

# F1 Validation names two assets by where they were cooked; these are the authored assets they came from.
OUTPUT_REFERENCES = {
    ".cooked/Scripts/rotate.oscript": "/Showcase/Scenes/RotatingGltf/rotate.oscript",
    ".cooked/Input/rotate.input/RotateInputContext.oimap": "/Showcase/Scenes/RotatingGltf/rotate.input/RotateInputContext.oimap",
}


@dataclass
class Migration:
    """What one project's migration changes and what it could not resolve."""

    writes: dict[str, str] = field(default_factory=dict[str, str])
    deletes: set[str] = field(default_factory=set[str])
    unresolved: list[str] = field(default_factory=list[str])
    notes: list[str] = field(default_factory=list[str])


def load(text: str) -> JsonObject:
    """Parses authored JSON, tolerating the byte order mark some files carry."""
    return cast(JsonObject, json.loads(text.lstrip("﻿")))


def dump(document: JsonObject) -> str:
    """Spells JSON as the editor does: two spaces, plain Unicode, a final line break."""
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def to_reference(anchor: str, target: str) -> str:
    """Spells target from the file whose identity is anchor: relative in its mount, absolute otherwise."""
    from_parts = anchor.lstrip("/").split("/")
    to_parts = target.lstrip("/").split("/")
    if from_parts[0] != to_parts[0]:
        return target
    folder = from_parts[:-1]
    common = 0
    while (
        common < len(folder)
        and common < len(to_parts) - 1
        and folder[common] == to_parts[common]
    ):
        common += 1
    return "/".join([".."] * (len(folder) - common) + to_parts[common:])


def identity_of(virtual_path: str) -> str:
    """Removes the authoring suffix: an identity names an asset, not its descriptor file."""
    for suffix in DESCRIPTOR_SUFFIXES:
        if virtual_path.endswith(suffix):
            return virtual_path[: -len(".json")]
    return virtual_path


def own_mounts(project: JsonObject) -> dict[str, str]:
    """Gets the mounts whose folders the project commits, by name: those it may rewrite."""
    return {
        mount["Name"]: mount["Path"].strip("/")
        for mount in project.get("Mounts", [])
        if mount.get("Kind", "Source") == "Source"
        and mount.get("Path")
        and not mount["Path"].startswith("..")
    }


def virtual_path_of(path: str, mounts: dict[str, str]) -> str | None:
    """Gets a project-relative file's virtual path in the mount that holds it."""
    for name, folder in mounts.items():
        if path.startswith(folder + "/"):
            return f"/{name}/{path[len(folder) + 1 :]}"
    return None


def file_of(virtual_path: str, mounts: dict[str, str]) -> str | None:
    """Gets the project-relative file of a virtual path in an own mount."""
    mount, _, rest = virtual_path.lstrip("/").partition("/")
    return f"{mounts[mount]}/{rest}" if mount in mounts else None


def migrate_project_file(text: str) -> JsonObject:
    """Moves Project.oxy to the current schema; a Pak mount becomes Cooked, read by its binding."""
    project = load(text)
    project["SchemaVersion"] = CURRENT_PROJECT_SCHEMA
    for mount in project.get("Mounts", []):
        if mount.get("Kind") == "Pak":
            mount["Kind"] = "Cooked"
    return project


def migrate_mounts_file(text: str) -> str:
    """States a mounts file's version under the one version key."""
    document = load(text)
    mounts = document.get("mounts", {})
    rest = {
        key: value
        for key, value in document.items()
        if key not in {"version", "schema_version", "mounts"}
    }
    return dump({"schema_version": MOUNTS_SCHEMA, **rest, "mounts": mounts})


def is_retired_sidecar(text: str) -> bool:
    """Tells an old editor sidecar from a record: it states SchemaVersion, a record schema_version."""
    return "SchemaVersion" in load(text)


def record_target(
    record: JsonObject, relative: str, mounts: dict[str, str], moved: dict[str, str]
) -> str:
    """Places a record at its identity; a model in a mount the project does not own moves into the first own mount."""
    identity = "/" + relative[: -len(RECORD_SUFFIX)]
    mount = identity.lstrip("/").split("/", 1)[0]
    if mount in mounts:
        return identity
    name = identity.rsplit("/", 1)[1]
    new_identity = f"/{next(iter(mounts))}/{name.rsplit('.', 1)[0]}/{name}"
    moved[identity] = new_identity
    record.setdefault("source", identity)
    return new_identity


def respell_scene_value(
    value: str,
    anchor: str,
    moved: dict[str, str],
    tree: dict[str, str],
    mounts: dict[str, str],
    migration: Migration,
    where: str,
) -> str:
    """Turns an asset URI into reference text, resolving an old output path to the source it cooks from."""
    target = unquote(value[len(ASSET_URI) :])
    if target in OUTPUT_REFERENCES:
        return to_reference(anchor, OUTPUT_REFERENCES[target])
    if target.startswith(OUTPUT_PREFIX):
        rest = target[len(OUTPUT_PREFIX) :]
        first_own = next(iter(mounts))
        candidate = f"/{first_own}/{rest}"
        if not source_exists(candidate, tree, mounts):
            migration.unresolved.append(
                f"{where}: '{value}' names an output folder, and no source cooks to '{candidate}'"
            )
        target = candidate.lstrip("/")
    identity = identity_of("/" + target)
    for old, new in moved.items():
        if identity == old or identity.startswith(old + "/"):
            identity = new + identity[len(old) :]
    return to_reference(anchor, identity)


def source_exists(identity: str, tree: dict[str, str], mounts: dict[str, str]) -> bool:
    """Tells whether an own mount holds a file that cooks to an identity, itself or the container above it."""
    file = file_of(identity, mounts)
    if file is None:
        return True
    while "/" in file:
        if file + ".json" in tree or file in tree:
            return True
        file = file.rsplit("/", 1)[0]
    return False


def migrate_scene(
    text: str,
    anchor: str,
    moved: dict[str, str],
    tree: dict[str, str],
    mounts: dict[str, str],
    migration: Migration,
    where: str,
) -> str:
    """Respells every asset URI of a scene document; only reference fields hold them."""

    def walk(node: Any) -> Any:
        if isinstance(node, str):
            return (
                respell_scene_value(node, anchor, moved, tree, mounts, migration, where)
                if node.startswith(ASSET_URI)
                else node
            )
        if isinstance(node, list):
            return [walk(item) for item in cast(list[Any], node)]
        if isinstance(node, dict):
            return {key: walk(item) for key, item in cast(JsonObject, node).items()}
        return node

    return dump(cast(JsonObject, walk(load(text))))


def migrate_descriptor(text: str, path: str, anchor: str) -> str:
    """Removes a repeated identity and respells a descriptor's references from its own identity."""
    document = load(text)
    if path.endswith(".otex.json"):
        document.pop("virtual_path", None)
        for key in ("source",):
            if isinstance(document.get(key), str) and document[key].startswith("/"):
                document[key] = to_reference(anchor, document[key])
        for source in document.get("sources", []):
            if isinstance(source, dict) and str(
                cast(JsonObject, source).get("file", "")
            ).startswith("/"):
                cast(JsonObject, source)["file"] = to_reference(
                    anchor, cast(JsonObject, source)["file"]
                )
    elif path.endswith(".omat.json"):
        for texture in document.get("textures", {}).values():
            if isinstance(texture, dict) and str(
                cast(JsonObject, texture).get("virtual_path", "")
            ).startswith("/"):
                cast(JsonObject, texture)["virtual_path"] = to_reference(
                    anchor, cast(JsonObject, texture)["virtual_path"]
                )
    return dump(document)


def migrate_tree(tree: dict[str, str], files: set[str]) -> Migration:
    """Migrates one project: tree holds its authored text by relative path, files every file it has."""
    migration = Migration()
    project = migrate_project_file(tree["Project.oxy"])
    migration.writes["Project.oxy"] = dump(project)
    if ".mounts.json" in tree:
        migration.writes[".mounts.json"] = migrate_mounts_file(tree[".mounts.json"])
    mounts = own_mounts(project)
    if not mounts:
        migration.notes.append(
            "The project commits no source folder: nothing else to rewrite."
        )
        return migration

    # Sidecars of the retired formats go first: some sit where records move to.
    for path, text in tree.items():
        if (
            path.endswith(RECORD_SUFFIX)
            and not path.startswith("Imports/")
            and is_retired_sidecar(text)
        ):
            migration.deletes.add(path)

    moved: dict[str, str] = {}
    for path, text in sorted(tree.items()):
        if not (path.startswith("Imports/") and path.endswith(RECORD_SUFFIX)):
            continue
        record = load(text)
        relative = path[len("Imports/") :]
        identity = record_target(record, relative, mounts, moved)
        record.pop("virtual_path", None)
        source = str(record.get("source", ""))
        if source.startswith("/"):
            if (
                file_of(source, mounts) is not None
                and cast(str, file_of(source, mounts)) not in files
            ):
                migration.unresolved.append(
                    f"{path}: its source '{source}' is not in the project"
                )
            record["source"] = to_reference(identity, source)
        target = cast(str, file_of(identity, mounts)) + RECORD_SUFFIX
        if target in tree and target not in migration.deletes:
            migration.unresolved.append(
                f"{path}: '{target}' already holds a record; this one was not moved"
            )
            continue
        migration.writes[target] = dump(record)
        migration.deletes.discard(target)
        migration.deletes.add(path)

    for path, text in sorted(tree.items()):
        virtual = virtual_path_of(path, mounts)
        if virtual is None or path in migration.deletes or path in migration.writes:
            continue
        anchor = identity_of(virtual)
        if path.endswith(".oscene.json"):
            migration.writes[path] = migrate_scene(
                text, anchor, moved, tree, mounts, migration, path
            )
        elif path.endswith((".otex.json", ".omat.json")):
            migration.writes[path] = migrate_descriptor(text, path, anchor)

    migration.writes = {
        path: text
        for path, text in migration.writes.items()
        if path not in tree or load(tree[path]) != load(text)
    }
    return migration


def load_tree(project: Path) -> tuple[dict[str, str], set[str]]:
    """Reads a project's authored text and lists its files, leaving out generated folders."""
    tree: dict[str, str] = {}
    files: set[str] = set()
    for path in project.rglob("*"):
        relative = path.relative_to(project)
        if path.is_dir() or SKIPPED_FOLDERS.intersection(relative.parts[:-1]):
            continue
        key = relative.as_posix()
        files.add(key)
        if path.suffix in TREE_SUFFIXES:
            tree[key] = path.read_text(encoding="utf-8-sig")
    return tree, files


def back_up(project: Path, backups: Path) -> None:
    """Copies a project's authored files, which nothing else keeps; generated folders can be made again."""
    target = backups / project.name
    if not target.exists():
        shutil.copytree(
            project, target, ignore=shutil.ignore_patterns(".cooked", ".build")
        )


def apply(project: Path, migration: Migration) -> None:
    """Writes a migration, removes what it replaced and the generated output of the old forms."""
    for relative, text in migration.writes.items():
        target = project / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
    for relative in migration.deletes:
        (project / relative).unlink(missing_ok=True)
    imports = project / "Imports"
    if imports.exists():
        remaining = [path for path in imports.rglob("*") if path.is_file()]
        if remaining:
            migration.notes.append(
                f"Imports/ kept: it still holds {[path.name for path in remaining]}"
            )
        else:
            shutil.rmtree(imports)
    for relative in (".cooked", *STALE_BUILD_FOLDERS):
        shutil.rmtree(project / relative, ignore_errors=True)
    for inputs in (project / ".build" / "cook").glob("*/inputs/Imports"):
        shutil.rmtree(inputs, ignore_errors=True)


def run(root: Path, backups: Path, *, write: bool) -> list[JsonObject]:
    """Migrates each project under root; one on the current schema only loses its cooked folder."""
    report: list[JsonObject] = []
    for project in sorted(
        path for path in root.iterdir() if (path / "Project.oxy").exists()
    ):
        tree, files = load_tree(project)
        version = load(tree["Project.oxy"]).get("SchemaVersion")
        migration = (
            Migration()
            if version == CURRENT_PROJECT_SCHEMA
            else migrate_tree(tree, files)
        )
        if write:
            back_up(project, backups)
            apply(project, migration)
        report.append(
            {
                "project": project.name,
                "schema_before": version,
                "writes": sorted(migration.writes),
                "deletes": sorted(migration.deletes),
                "unresolved": migration.unresolved,
                "notes": migration.notes,
            }
        )
    return report


def main(argv: list[str] | None = None) -> int:
    """Runs the migration; without --apply it only reports."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, required=True, help="The folder that holds the projects."
    )
    parser.add_argument(
        "--backups", type=Path, help="Where the authored files are copied first."
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Write the changes and delete the cooked folders.",
    )
    arguments = parser.parse_args(argv)
    backups = cast(
        Path,
        arguments.backups
        or arguments.root.with_name(arguments.root.name + ".pre-portable-references"),
    )
    report = run(cast(Path, arguments.root), backups, write=cast(bool, arguments.apply))
    json.dump(report, sys.stdout, indent=2, ensure_ascii=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
