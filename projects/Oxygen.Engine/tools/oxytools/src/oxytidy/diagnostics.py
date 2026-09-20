"""Structured clang-tidy diagnostics with stable identity and origin tracking."""

from __future__ import annotations

from pathlib import Path

from oxytools.common import ToolError, absolute, digest, validate, yaml_documents

from .scope import Scope

REPLACEMENT = {
    "type": "object",
    "required": ["FilePath", "Offset", "Length", "ReplacementText"],
    "properties": {
        "FilePath": {"type": "string"},
        "Offset": {"type": "integer", "minimum": 0},
        "Length": {"type": "integer", "minimum": 0},
        "ReplacementText": {"type": "string"},
    },
}
MESSAGE = {
    "type": "object",
    "required": ["Message"],
    "properties": {
        "Message": {"type": "string"},
        "FilePath": {"type": "string"},
        "FileOffset": {"type": "integer", "minimum": 0},
        "Replacements": {"type": "array", "items": REPLACEMENT},
    },
}
EXPORT_SCHEMA = {
    "type": "object",
    "required": ["Diagnostics"],
    "properties": {
        "Diagnostics": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["DiagnosticName", "DiagnosticMessage"],
                "properties": {
                    "DiagnosticName": {"type": "string"},
                    "DiagnosticMessage": MESSAGE,
                    "Level": {"enum": ["Warning", "Error", "Remark", "Note"]},
                    "Notes": {"type": "array", "items": MESSAGE},
                    "BuildDirectory": {"type": "string"},
                },
            },
        },
    },
}

LOCATION_SCHEMA = {
    "type": "object",
    "required": ["file", "offset", "line", "column", "message"],
    "properties": {
        "file": {"type": "string"},
        "message": {"type": "string"},
        **{
            key: {"type": "integer", "minimum": 0}
            for key in ("offset", "line", "column")
        },
    },
}
DIAGNOSTIC_SCHEMA = {
    "type": "object",
    "required": [
        *LOCATION_SCHEMA["required"],
        "id",
        "check",
        "level",
        "notes",
        "replacements",
        "origins",
    ],
    "properties": {
        **LOCATION_SCHEMA["properties"],
        "id": {"type": "string", "minLength": 1},
        "check": {"type": "string"},
        "level": {"enum": ["warning", "error", "remark", "note"]},
        "notes": {"type": "array", "items": LOCATION_SCHEMA},
        "origins": {"type": "array", "minItems": 1, "items": {"type": "string"}},
        "replacements": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["file", "offset", "length", "text"],
                "properties": {
                    "file": {"type": "string"},
                    "text": {"type": "string"},
                    "offset": {"type": "integer", "minimum": 0},
                    "length": {"type": "integer", "minimum": 0},
                },
            },
        },
    },
}


def location(message: dict, directory: Path) -> dict:
    name = message.get("FilePath", "")
    path = absolute(name, directory) if name else None
    offset = message.get("FileOffset", 0)
    line = column = 0
    if path and path.is_file():
        prefix = path.read_bytes()[:offset]
        line = prefix.count(b"\n") + 1
        column = len(prefix.rsplit(b"\n", 1)[-1]) + 1
    return {
        "file": str(path) if path else "",
        "offset": offset,
        "line": line,
        "column": column,
        "message": message["Message"],
    }


def read_export(path: Path, directory: Path, origin: str) -> list[dict]:
    if not path.exists() or not path.stat().st_size:
        return []
    documents = yaml_documents(path.read_text(encoding="utf-8-sig"))
    if len(documents) != 1:
        raise ToolError(f"Expected one diagnostic export document: {path}")
    data = documents[0]
    validate(data, EXPORT_SCHEMA, str(path))
    diagnostics = []
    for item in data["Diagnostics"]:
        base = absolute(item.get("BuildDirectory") or str(directory), directory)
        message = item["DiagnosticMessage"]
        record = location(message, base)
        replacements = [
            {
                "file": str(absolute(rep["FilePath"], base)),
                "offset": rep["Offset"],
                "length": rep["Length"],
                "text": rep["ReplacementText"],
            }
            for rep in message.get("Replacements", [])
        ]
        record.update(
            {
                "check": item["DiagnosticName"],
                "level": item.get("Level", "Warning").lower(),
                "notes": [location(note, base) for note in item.get("Notes", [])],
                "replacements": replacements,
                "origins": [origin],
            }
        )
        record["id"] = digest(
            [record[key] for key in ("file", "offset", "check", "level", "message")]
        )
        diagnostics.append(record)
    return diagnostics


def scoped(records: list[dict], scope: Scope) -> list[dict]:
    return [
        record
        for record in records
        if record["file"] and scope.contains(Path(record["file"]))
    ]


def deduplicate(records: list[dict]) -> list[dict]:
    unique = {}
    for record in records:
        if record["id"] not in unique:
            unique[record["id"]] = {
                **record,
                "origins": list(record["origins"]),
                "notes": list(record["notes"]),
                "replacement_sets": [],
            }
        target = unique[record["id"]]
        target["origins"] = sorted(set(target["origins"] + record["origins"]))
        for note in record["notes"]:
            if note not in target["notes"]:
                target["notes"].append(note)
        if record["replacements"] not in target["replacement_sets"]:
            target["replacement_sets"].append(record["replacements"])
    return sorted(
        unique.values(), key=lambda item: (item["file"], item["offset"], item["check"])
    )
