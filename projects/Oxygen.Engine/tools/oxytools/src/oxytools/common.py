"""Shared path, serialization, and validation contracts."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any

import jsonschema
import yaml


class ToolError(Exception):
    """An actionable input, coverage, or execution failure."""


class UniqueKeyLoader(yaml.SafeLoader):
    """Reject duplicate YAML keys instead of silently replacing settings."""


def _mapping(loader: UniqueKeyLoader, node: yaml.MappingNode) -> dict:
    loader.flatten_mapping(node)
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node)
        if not isinstance(key, (str, int, float, bool)) or key in result:
            raise ToolError(
                f"Invalid or duplicate YAML key at line {key_node.start_mark.line + 1}"
            )
        result[key] = loader.construct_object(value_node)
    return result


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping
)


def yaml_documents(text: str) -> list[Any]:
    try:
        return list(yaml.load_all(text, Loader=UniqueKeyLoader))
    except yaml.YAMLError as error:
        raise ToolError(f"Invalid YAML: {error}") from error


def validate(value: Any, schema: dict, label: str) -> None:
    try:
        jsonschema.Draft202012Validator(schema).validate(value)
    except jsonschema.ValidationError as error:
        location = ".".join(map(str, error.absolute_path)) or "root"
        raise ToolError(f"{label} ({location}): {error.message}") from error


def absolute(value: str | Path, directory: Path) -> Path:
    path = Path(value)
    return (directory / path).resolve()


def path_key(path: str | Path) -> str:
    return os.path.normcase(str(Path(path).resolve()))


def within(path: Path, root: Path) -> bool:
    return path.resolve().is_relative_to(root.resolve())


def display(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix() if within(path, root) else str(path)


def digest(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def file_hash(path: str | Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fingerprint(paths: list[Path]) -> dict[str, str]:
    return {path_key(path): file_hash(path) for path in sorted(set(paths))}


def unchanged(snapshot: dict[str, str]) -> bool:
    try:
        return all(file_hash(path) == expected for path, expected in snapshot.items())
    except OSError:
        return False


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
