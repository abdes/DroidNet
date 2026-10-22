# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Loads and checks the scene components schema."""

# pyright: strict

from __future__ import annotations

import json
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

import jsonschema
import yaml

# The value types whose value goes to one field each, by number of fields.
_FIELD_COUNTS = {"bool": 1, "int": 1, "float": 1, "enum": 1, "vec3": 3}


class SchemaError(ValueError):
    """The schema file is malformed or inconsistent."""


@dataclass(frozen=True)
class Field:
    """A component-local field id."""

    name: str
    id: int


@dataclass(frozen=True)
class FieldSet:
    """The field ids of one or more components."""

    name: str
    fields: tuple[Field, ...]

    @property
    def count(self) -> int:
        """One past the largest field id."""
        return max(field.id for field in self.fields) + 1

    def ordered(self) -> tuple[Field, ...]:
        """The fields by id."""
        return tuple(sorted(self.fields, key=lambda field: field.id))


@dataclass(frozen=True)
class Wire:
    """How a component is applied field by field."""

    id: int
    field_set: str


@dataclass(frozen=True)
class Property:
    """An authored property of a component."""

    name: str
    key: str
    type: str
    enum: str | None
    fields: tuple[str, ...]
    encoding: str | None


@dataclass(frozen=True)
class Component:
    """An authored scene node component."""

    name: str
    kind: str
    wire: Wire | None
    properties: tuple[Property, ...]


@dataclass(frozen=True)
class Schema:
    """The scene components schema."""

    field_sets: tuple[FieldSet, ...]
    components: tuple[Component, ...]

    def wired(self) -> tuple[tuple[Component, Wire], ...]:
        """The components applied field by field, by component id."""
        pairs = [
            (component, component.wire)
            for component in self.components
            if component.wire is not None
        ]
        return tuple(sorted(pairs, key=lambda pair: pair[1].id))


def load(path: Path, json_schema: Path) -> Schema:
    """Reads `path`, validates it against `json_schema` and checks it."""
    with path.open("r", encoding="utf-8") as stream:
        document: object = yaml.safe_load(stream)
    rules: object = json.loads(json_schema.read_text(encoding="utf-8"))
    return parse(document, rules)


def parse(document: object, rules: object) -> Schema:
    """Validates a loaded document against `rules` and checks it."""
    try:
        jsonschema.validate(
            document, cast(Mapping[str, Any], rules), cls=jsonschema.Draft7Validator
        )
    except jsonschema.ValidationError as error:
        raise SchemaError(f"{_where(error.path)}: {error.message}") from error
    data = cast(Mapping[str, Any], document)
    schema = Schema(
        field_sets=tuple(_field_set(item) for item in data["field_sets"]),
        components=tuple(_component(item) for item in data["components"]),
    )
    _check(schema)
    return schema


def _where(path: Iterable[object]) -> str:
    return "/" + "/".join(str(part) for part in path)


def _field_set(item: Mapping[str, Any]) -> FieldSet:
    return FieldSet(
        name=item["name"],
        fields=tuple(
            Field(name=field["name"], id=field["id"]) for field in item["fields"]
        ),
    )


def _component(item: Mapping[str, Any]) -> Component:
    wire = cast(Mapping[str, Any] | None, item.get("wire"))
    return Component(
        name=item["name"],
        kind=item["kind"],
        wire=None if wire is None else Wire(id=wire["id"], field_set=wire["field_set"]),
        properties=tuple(_property(entry) for entry in item["properties"]),
    )


def _property(item: Mapping[str, Any]) -> Property:
    return Property(
        name=item["name"],
        key=item["key"],
        type=item["type"],
        enum=item.get("enum"),
        fields=tuple(item.get("fields", ())),
        encoding=item.get("encoding"),
    )


def _duplicates(values: Sequence[object]) -> list[str]:
    seen: set[object] = set()
    repeated: list[str] = []
    for value in values:
        if value in seen:
            repeated.append(str(value))
        seen.add(value)
    return repeated


def _check_unique(what: str, values: Sequence[object]) -> None:
    repeated = _duplicates(values)
    if repeated:
        raise SchemaError(f"duplicate {what}: {', '.join(repeated)}")


def _check(schema: Schema) -> None:
    _check_unique("field set", [field_set.name for field_set in schema.field_sets])
    for field_set in schema.field_sets:
        _check_unique(
            f"field name in {field_set.name}",
            [field.name for field in field_set.fields],
        )
        _check_unique(
            f"field id in {field_set.name}", [field.id for field in field_set.fields]
        )
    _check_unique("component", [component.name for component in schema.components])
    _check_unique("component kind", [component.kind for component in schema.components])
    _check_unique(
        "component id",
        [wire.id for _, wire in schema.wired()],
    )
    field_sets = {field_set.name: field_set for field_set in schema.field_sets}
    for component in schema.components:
        _check_component(component, field_sets)


def _check_component(component: Component, field_sets: Mapping[str, FieldSet]) -> None:
    where = component.name
    _check_unique(
        f"property in {where}", [entry.name for entry in component.properties]
    )
    _check_unique(f"key in {where}", [entry.key for entry in component.properties])
    if component.wire is None:
        for entry in component.properties:
            if entry.fields:
                raise SchemaError(f"{where}.{entry.name}: fields without a wire")
        return
    field_set = field_sets.get(component.wire.field_set)
    if field_set is None:
        raise SchemaError(f"{where}: unknown field set {component.wire.field_set}")
    names = {field.name for field in field_set.fields}
    for entry in component.properties:
        expected = _FIELD_COUNTS.get(entry.type)
        if expected is None:
            raise SchemaError(
                f"{where}.{entry.name}: {entry.type} values have no fields"
            )
        if len(entry.fields) != expected:
            raise SchemaError(
                f"{where}.{entry.name}: {entry.type} values go to {expected} fields, "
                f"not {len(entry.fields)}"
            )
        unknown = [name for name in entry.fields if name not in names]
        if unknown:
            raise SchemaError(
                f"{where}.{entry.name}: not in {field_set.name}: {', '.join(unknown)}"
            )
    _check_unique(
        f"field use in {where}",
        [name for entry in component.properties for name in entry.fields],
    )
