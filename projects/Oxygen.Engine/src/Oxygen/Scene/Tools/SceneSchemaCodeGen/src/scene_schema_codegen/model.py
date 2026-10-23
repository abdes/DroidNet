# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Loads and checks the scene components schema."""

# pyright: strict

from __future__ import annotations

import json
import math
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

import jsonschema
import yaml

# The value types whose value goes to one field each, by number of fields.
_FIELD_COUNTS = {"bool": 1, "int": 1, "float": 1, "enum": 1, "vec3": 3}

# The value types a range bounds.
NUMERIC_TYPES = frozenset({"int", "float", "vec3"})

# The units a property may be shown in other than its own, by stored unit.
_DISPLAY_UNITS = {"radian": frozenset({"degree"})}

# The unit of the authored value an encoding converts.
_ENCODING_UNITS = {"degrees-to-radians": "degree"}


class SchemaError(ValueError):
    """The schema file is malformed or inconsistent."""


@dataclass(frozen=True)
class Unit:
    """A unit that UIs show."""

    id: str
    abbreviation: str
    name: str


@dataclass(frozen=True)
class Member:
    """A member of an enumeration, with its wire value."""

    name: str
    value: int


@dataclass(frozen=True)
class Enumeration:
    """An enumeration of enum properties."""

    name: str
    members: tuple[Member, ...]

    def value_of(self, member: str) -> int | None:
        """The wire value of `member`, or None if it is not declared."""
        return next(
            (entry.value for entry in self.members if entry.name == member), None
        )


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
class Range:
    """The valid values of a numeric property; None bounds are unlimited."""

    min: float | None
    max: float | None
    min_exclusive: bool
    max_exclusive: bool
    nonzero: bool

    def contains(self, value: float) -> bool:
        """Whether `value` is finite and inside the range."""
        if not math.isfinite(value) or (self.nonzero and value == 0):
            return False
        if self.min is not None and (
            value < self.min or (self.min_exclusive and value == self.min)
        ):
            return False
        return self.max is None or not (
            value > self.max or (self.max_exclusive and value == self.max)
        )


UNLIMITED = Range(None, None, False, False, False)


@dataclass(frozen=True)
class LodPolicy:
    """A geometry LOD policy value."""

    type: str
    lod_index: int
    thresholds: tuple[float, ...]
    hysteresis_ratio: float
    enter_finer_sse: tuple[float, ...]
    exit_coarser_sse: tuple[float, ...]


# A property value in authored units: a bool, an int, a float, three floats,
# an enumeration member name or a LOD policy.
Value = bool | int | float | tuple[float, ...] | str | LodPolicy


@dataclass(frozen=True)
class Property:
    """An authored property of a component."""

    name: str
    key: str
    type: str
    enum: str | None
    fields: tuple[str, ...]
    encoding: str | None
    sources: tuple[str, ...]
    override_id: int | None
    default: Value | None
    root_default: str | None
    range: Range | None
    commit: str
    unit: str | None
    display_unit: str | None

    def components(self) -> tuple[float, ...]:
        """The numeric default by field, in authored units."""
        value = self.default
        if isinstance(value, tuple):
            return value
        assert isinstance(value, (int, float)) and not isinstance(value, bool)
        return (float(value),)


@dataclass(frozen=True)
class Part:
    """A piece of a component, identified by the asset that defines it."""

    name: str
    kind: str
    properties: tuple[Property, ...]


@dataclass(frozen=True)
class Component:
    """An authored scene node component."""

    name: str
    kind: str
    cardinality: str
    wire: Wire | None
    properties: tuple[Property, ...]
    parts: tuple[Part, ...]


@dataclass(frozen=True)
class Override:
    """A property a scene stores as overrides, with its address."""

    component: Component
    part: Part | None
    prop: Property

    @property
    def id(self) -> int:
        """The override id."""
        assert self.prop.override_id is not None
        return self.prop.override_id


@dataclass(frozen=True)
class Schema:
    """The scene components schema."""

    units: tuple[Unit, ...]
    enums: tuple[Enumeration, ...]
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

    def part_kinds(self) -> tuple[str, ...]:
        """The part kinds, in declaration order."""
        kinds: list[str] = []
        for component in self.components:
            kinds.extend(
                part.kind for part in component.parts if part.kind not in kinds
            )
        return tuple(kinds)

    def overrides(self) -> tuple[Override, ...]:
        """The properties stored as overrides, by override id."""
        found: list[Override] = []
        for component in self.components:
            found.extend(
                Override(component, None, entry)
                for entry in component.properties
                if entry.override_id is not None
            )
            for part in component.parts:
                found.extend(
                    Override(component, part, entry)
                    for entry in part.properties
                    if entry.override_id is not None
                )
        return tuple(sorted(found, key=lambda entry: entry.id))

    def enumeration(self, name: str) -> Enumeration:
        """The enumeration called `name`."""
        return next(entry for entry in self.enums if entry.name == name)


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
        units=tuple(
            Unit(id=item["id"], abbreviation=item["abbreviation"], name=item["name"])
            for item in data["units"]
        ),
        enums=tuple(_enumeration(item) for item in data["enums"]),
        field_sets=tuple(_field_set(item) for item in data["field_sets"]),
        components=tuple(_component(item) for item in data["components"]),
    )
    _check(schema)
    return schema


def _where(path: Iterable[object]) -> str:
    return "/" + "/".join(str(part) for part in path)


def _enumeration(item: Mapping[str, Any]) -> Enumeration:
    return Enumeration(
        name=item["name"],
        members=tuple(
            Member(name=member["name"], value=member["value"])
            for member in item["members"]
        ),
    )


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
        cardinality=item["cardinality"],
        wire=None if wire is None else Wire(id=wire["id"], field_set=wire["field_set"]),
        properties=tuple(_property(entry) for entry in item["properties"]),
        parts=tuple(_part(entry) for entry in item.get("parts", ())),
    )


def _part(item: Mapping[str, Any]) -> Part:
    return Part(
        name=item["name"],
        kind=item["kind"],
        properties=tuple(_property(entry) for entry in item["properties"]),
    )


def _property(item: Mapping[str, Any]) -> Property:
    bounds = cast(Mapping[str, Any] | None, item.get("range"))
    return Property(
        name=item["name"],
        key=item["key"],
        type=item["type"],
        enum=item.get("enum"),
        fields=tuple(item.get("fields", ())),
        encoding=item.get("encoding"),
        sources=tuple(item.get("sources", ())),
        override_id=item.get("override_id"),
        default=_value(item["type"], item.get("default")),
        root_default=item.get("root_default"),
        range=None if bounds is None else _range(bounds),
        commit=item.get("commit", "reject"),
        unit=item.get("unit"),
        display_unit=item.get("display_unit"),
    )


def _range(item: Mapping[str, Any]) -> Range:
    low = cast(float | None, item.get("min"))
    high = cast(float | None, item.get("max"))
    return Range(
        min=None if low is None else float(low),
        max=None if high is None else float(high),
        min_exclusive=item.get("min_exclusive", False),
        max_exclusive=item.get("max_exclusive", False),
        nonzero=item.get("nonzero", False),
    )


def _value(value_type: str, value: object) -> Value | None:
    if value is None:
        return None
    if value_type == "vec3":
        return tuple(float(entry) for entry in cast(Sequence[float], value))
    if value_type == "float":
        return float(cast(float, value))
    if value_type == "lod-policy":
        policy = cast(Mapping[str, Any], value)
        return LodPolicy(
            type=policy["type"],
            lod_index=policy.get("lod_index", 0),
            thresholds=tuple(policy.get("thresholds", ())),
            hysteresis_ratio=float(policy.get("hysteresis_ratio", 0.0)),
            enter_finer_sse=tuple(policy.get("enter_finer_sse", ())),
            exit_coarser_sse=tuple(policy.get("exit_coarser_sse", ())),
        )
    return cast(bool | int | str, value)


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
    _check_unique("unit", [unit.id for unit in schema.units])
    _check_unique("enumeration", [entry.name for entry in schema.enums])
    for enumeration in schema.enums:
        _check_unique(
            f"member in {enumeration.name}",
            [member.name for member in enumeration.members],
        )
        _check_unique(
            f"member value in {enumeration.name}",
            [member.value for member in enumeration.members],
        )
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
        for entry in component.properties:
            _check_value(schema, f"{component.name}.{entry.name}", entry)
        for part in component.parts:
            for entry in part.properties:
                _check_value(
                    schema, f"{component.name}.{part.name}.{entry.name}", entry
                )
    _check_unique("override id", [entry.id for entry in schema.overrides()])


def _check_component(component: Component, field_sets: Mapping[str, FieldSet]) -> None:
    where = component.name
    _check_unique(
        f"property in {where}", [entry.name for entry in component.properties]
    )
    _check_unique(f"key in {where}", [entry.key for entry in component.properties])
    _check_parts(component)
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


def _check_parts(component: Component) -> None:
    # A part's name scopes its properties' names, so it cannot be a property's.
    where = component.name
    _check_unique(
        f"part or property in {where}",
        [entry.name for entry in component.properties]
        + [part.name for part in component.parts],
    )
    _check_unique(f"part kind in {where}", [part.kind for part in component.parts])
    for part in component.parts:
        _check_unique(
            f"property in {where}.{part.name}",
            [entry.name for entry in part.properties],
        )
        _check_unique(
            f"key in {where}.{part.name}", [entry.key for entry in part.properties]
        )


def _check_value(schema: Schema, where: str, entry: Property) -> None:
    _check_units(schema, where, entry)
    if entry.type == "enum":
        _check_enum(schema, where, entry)
    elif entry.root_default is not None:
        raise SchemaError(f"{where}: root_default on a {entry.type} property")
    if entry.type in NUMERIC_TYPES:
        _check_numeric(where, entry)
    if isinstance(entry.default, LodPolicy):
        _check_lod_policy(where, entry.default)


def _check_units(schema: Schema, where: str, entry: Property) -> None:
    units = {unit.id for unit in schema.units}
    for unit in (entry.unit, entry.display_unit):
        if unit is not None and unit not in units:
            raise SchemaError(f"{where}: unknown unit {unit}")
    if entry.display_unit is not None and entry.display_unit not in _DISPLAY_UNITS.get(
        entry.unit or "", frozenset()
    ):
        raise SchemaError(
            f"{where}: {entry.unit} values cannot be shown in {entry.display_unit}"
        )
    if entry.encoding is not None:
        if entry.type != "float":
            raise SchemaError(f"{where}: {entry.encoding} encodes float values only")
        if entry.unit != _ENCODING_UNITS[entry.encoding]:
            expected = _ENCODING_UNITS[entry.encoding]
            raise SchemaError(f"{where}: {entry.encoding} needs unit {expected}")


def _check_enum(schema: Schema, where: str, entry: Property) -> None:
    assert entry.enum is not None
    if entry.enum not in {enumeration.name for enumeration in schema.enums}:
        raise SchemaError(f"{where}: unknown enumeration {entry.enum}")
    enumeration = schema.enumeration(entry.enum)
    for member in (entry.default, entry.root_default):
        if member is not None and enumeration.value_of(cast(str, member)) is None:
            raise SchemaError(f"{where}: {member} is not a member of {entry.enum}")


def _check_numeric(where: str, entry: Property) -> None:
    bounds = entry.range or UNLIMITED
    if (
        bounds.min is not None
        and bounds.max is not None
        and (
            bounds.min > bounds.max
            or (
                bounds.min == bounds.max
                and (bounds.min_exclusive or bounds.max_exclusive)
            )
        )
    ):
        raise SchemaError(f"{where}: empty range")
    if entry.type == "int" and any(
        bound is not None and not bound.is_integer()
        for bound in (bounds.min, bounds.max)
    ):
        raise SchemaError(f"{where}: int range with a fractional bound")
    for value in entry.components():
        if not bounds.contains(value):
            raise SchemaError(f"{where}: default {value:g} is outside its range")


def _check_lod_policy(where: str, policy: LodPolicy) -> None:
    if len(policy.enter_finer_sse) != len(policy.exit_coarser_sse):
        raise SchemaError(
            f"{where}: enter_finer_sse and exit_coarser_sse differ in size"
        )
