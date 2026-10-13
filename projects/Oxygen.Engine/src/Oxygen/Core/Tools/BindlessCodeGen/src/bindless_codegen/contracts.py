# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#
# pyright: strict

"""Shared C++/HLSL shader contracts generated from one schema.

A contract schema lists the structs that cross the CPU/GPU boundary, grouped
into output files. For each struct the generator computes the HLSL layout of
its storage (structured buffer, constant buffer or byte-address buffer),
rejects layouts that would need implicit padding on either side, and emits:

- a C++ struct with default member initializers and `offsetof`, `sizeof` and
  `alignof` assertions, so a C++ type whose size or alignment differs fails
  the build;
- the HLSL struct (or `cbuffer`), an invalid-record maker for structs carrying
  bindless slots, domain-checked loaders, and byte offsets plus load/store
  functions for byte-address layouts.

Slot fields name the bindless domain they index (or `any` for descriptor
kinds without one, such as UAVs); loaders check the domain declared by the
contract against the bindless ABI (`Bindless.yaml`).
"""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import cast

import yaml

from .formatting import format_cpp, write_if_changed

_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_REGISTER = re.compile(r"^b[0-9]+$")
_CBUFFER_REGISTER_BYTES = 16
# A slot whose descriptor kind has no ABI domain (UAVs): it is still a
# bindless index, invalid by default, but no domain check applies.
ANY_DOMAIN = "any"

_LICENSE = """\
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
"""


class ContractError(ValueError):
    """The schema is malformed or describes a layout that cannot be shared."""


class Layout(Enum):
    """Storage a struct is read from; it decides the HLSL packing rules."""

    STRUCTURED = "structured"
    CONSTANT = "constant"
    BYTES = "bytes"


@dataclass(frozen=True)
class ScalarType:
    """A built-in HLSL scalar, vector or matrix type."""

    hlsl: str
    cpp: str
    cpp_include: str
    scalar: str
    components: int
    rows: int = 1

    @property
    def size(self) -> int:
        return self.scalar_size * self.components * self.rows

    @property
    def scalar_size(self) -> int:
        return 8 if self.scalar == "uint64" else 4

    @property
    def align(self) -> int:
        return self.scalar_size

    @property
    def is_matrix(self) -> bool:
        return self.rows > 1

    def cpp_default(self) -> str:
        if self.is_matrix:
            return "1.0F"
        if self.scalar == "float":
            return "0.0F"
        if self.scalar == "int":
            return "0"
        return "0U"


def _builtin_types() -> dict[str, ScalarType]:
    types: dict[str, ScalarType] = {
        "uint": ScalarType("uint", "std::uint32_t", "<cstdint>", "uint", 1),
        "int": ScalarType("int", "std::int32_t", "<cstdint>", "int", 1),
        "float": ScalarType("float", "float", "", "float", 1),
        "uint64_t": ScalarType("uint64_t", "std::uint64_t", "<cstdint>", "uint64", 1),
        "float4x4": ScalarType(
            "float4x4", "glm::mat4", "<glm/mat4x4.hpp>", "float", 4, 4
        ),
    }
    prefixes = {"float": "", "uint": "u", "int": "i"}
    for scalar, prefix in prefixes.items():
        for components in (2, 3, 4):
            name = f"{scalar}{components}"
            types[name] = ScalarType(
                name,
                f"glm::{prefix}vec{components}",
                f"<glm/vec{components}.hpp>",
                scalar,
                components,
            )
    return types


BUILTIN_TYPES = _builtin_types()


@dataclass(frozen=True)
class Constant:
    """A named value shared by C++ (`cpp`) and HLSL (`hlsl`)."""

    cpp: str
    hlsl: str
    type: str
    value: int | float
    doc: str = ""


@dataclass(frozen=True)
class Field:
    """One member of a contract struct."""

    name: str
    type: str
    count: int = 0
    slot: str = ""
    cpp: str = ""
    default: str = ""
    doc: str = ""


@dataclass(frozen=True)
class Loader:
    """A domain-checked HLSL loader; `array` loaders take an element index."""

    domain: str
    array: bool


@dataclass
class Struct:
    """A contract struct; `name` is the C++ name, `hlsl_name` the HLSL one."""

    name: str
    hlsl_name: str
    layout: Layout
    size: int
    align: int
    fields: list[Field]
    doc: str = ""
    register: str = ""
    loader: Loader | None = None
    hlsl_prefix: str = ""
    offsets: list[int] = field(default_factory=lambda: list[int]())


@dataclass
class ContractFile:
    """One pair of generated outputs."""

    id: str
    cpp: str
    hlsl: str
    namespace: str
    cpp_includes: list[str]
    hlsl_includes: list[str]
    constants: list[Constant]
    structs: list[Struct]


@dataclass
class Spec:
    """A parsed contract schema with the bindless domain tags it may use."""

    source: str
    files: list[ContractFile]
    domain_tags: dict[str, str]
    structs: dict[str, Struct] = field(default_factory=lambda: dict[str, Struct]())


# --- Parsing -----------------------------------------------------------------


def _require_mapping(value: object, where: str) -> Mapping[str, object]:
    if not isinstance(value, Mapping):
        raise ContractError(f"{where}: expected a mapping")
    result: dict[str, object] = {}
    for key, item in cast("Mapping[object, object]", value).items():
        if not isinstance(key, str):
            raise ContractError(f"{where}: keys must be strings")
        result[key] = item
    return result


def _require_list(value: object, where: str) -> list[object]:
    if value is None:
        return []
    if not isinstance(value, list):
        raise ContractError(f"{where}: expected a list")
    return list(cast("list[object]", value))


def _get_str(
    data: Mapping[str, object], key: str, where: str, default: str | None = None
) -> str:
    value = data.get(key, default)
    if not isinstance(value, str):
        raise ContractError(f"{where}: '{key}' must be a string")
    return value


def _get_int(
    data: Mapping[str, object], key: str, where: str, default: int | None = None
) -> int:
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{where}: '{key}' must be an integer")
    return value


def _check_keys(data: Mapping[str, object], allowed: set[str], where: str) -> None:
    unknown = sorted(set(data) - allowed)
    if unknown:
        raise ContractError(f"{where}: unknown keys {unknown}")


def _identifier(value: str, where: str) -> str:
    if not _IDENTIFIER.match(value):
        raise ContractError(f"{where}: '{value}' is not an identifier")
    return value


def upper_snake(name: str) -> str:
    """`ExposureStatusStorage` -> `EXPOSURE_STATUS_STORAGE`; `GlobalSRV` ->
    `GLOBAL_SRV` (the bindless ABI's domain tag spelling)."""
    camel = re.sub(
        r"([A-Z]{2,})(?![a-z])",
        lambda match: match.group(0)[0] + match.group(0)[1:].lower(),
        name,
    )
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", camel)
    return snake.upper()


def _parse_constant(raw: object, where: str) -> Constant:
    data = _require_mapping(raw, where)
    _check_keys(data, {"cpp", "hlsl", "type", "value", "doc"}, where)
    type_name = _get_str(data, "type", where)
    if type_name not in ("uint", "int", "float"):
        raise ContractError(f"{where}: constants are uint, int or float")
    value = data.get("value")
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ContractError(f"{where}: 'value' must be a number")
    if type_name != "float" and not isinstance(value, int):
        raise ContractError(f"{where}: integer constants need integer values")
    if type_name == "uint" and value < 0:
        raise ContractError(f"{where}: uint constants cannot be negative")
    return Constant(
        cpp=_identifier(_get_str(data, "cpp", where), where),
        hlsl=_identifier(_get_str(data, "hlsl", where), where),
        type=type_name,
        value=value,
        doc=_get_str(data, "doc", where, "").strip(),
    )


def _parse_field(raw: object, where: str) -> Field:
    data = _require_mapping(raw, where)
    _check_keys(data, {"name", "type", "count", "slot", "cpp", "default", "doc"}, where)
    name = _identifier(_get_str(data, "name", where), where)
    count = _get_int(data, "count", where, 0)
    if count < 0 or count == 1:
        raise ContractError(f"{where}: 'count' must be at least 2")
    return Field(
        name=name,
        type=_identifier(_get_str(data, "type", where), where),
        count=count,
        slot=_get_str(data, "slot", where, ""),
        cpp=_get_str(data, "cpp", where, ""),
        default=_get_str(data, "default", where, ""),
        doc=_get_str(data, "doc", where, "").strip(),
    )


def _parse_struct(raw: object, where: str) -> Struct:
    data = _require_mapping(raw, where)
    _check_keys(
        data,
        {
            "name",
            "hlsl_name",
            "layout",
            "size",
            "align",
            "fields",
            "doc",
            "register",
            "loader",
            "hlsl_prefix",
        },
        where,
    )
    name = _identifier(_get_str(data, "name", where), where)
    where = f"{where} ({name})"
    try:
        layout = Layout(_get_str(data, "layout", where))
    except ValueError as error:
        raise ContractError(f"{where}: unknown layout") from error
    align = _get_int(data, "align", where, 4)
    if align not in (4, 8, 16):
        raise ContractError(f"{where}: 'align' must be 4, 8 or 16")
    register = _get_str(data, "register", where, "")
    if register and (layout is not Layout.CONSTANT or not _REGISTER.match(register)):
        raise ContractError(f"{where}: 'register' names a b# slot of a constant layout")
    loader: Loader | None = None
    if "loader" in data:
        if layout is not Layout.STRUCTURED:
            raise ContractError(f"{where}: loaders read structured buffers")
        loader_data = _require_mapping(data["loader"], f"{where} loader")
        _check_keys(loader_data, {"domain", "array"}, f"{where} loader")
        array = loader_data.get("array", False)
        if not isinstance(array, bool):
            raise ContractError(f"{where} loader: 'array' must be a boolean")
        loader = Loader(
            domain=_get_str(loader_data, "domain", f"{where} loader"),
            array=array,
        )
    fields = [
        _parse_field(item, f"{where} field {index}")
        for index, item in enumerate(
            _require_list(data.get("fields"), f"{where} fields")
        )
    ]
    if not fields:
        raise ContractError(f"{where}: a struct needs fields")
    names = [item.name for item in fields]
    if len(set(names)) != len(names):
        raise ContractError(f"{where}: duplicate field names")
    return Struct(
        name=name,
        hlsl_name=_identifier(_get_str(data, "hlsl_name", where, name), where),
        layout=layout,
        size=_get_int(data, "size", where),
        align=align,
        fields=fields,
        doc=_get_str(data, "doc", where, "").strip(),
        register=register,
        loader=loader,
        hlsl_prefix=_get_str(data, "hlsl_prefix", where, ""),
    )


def _parse_file(raw: object, where: str) -> ContractFile:
    data = _require_mapping(raw, where)
    _check_keys(
        data,
        {
            "id",
            "cpp",
            "hlsl",
            "namespace",
            "cpp_includes",
            "hlsl_includes",
            "constants",
            "structs",
        },
        where,
    )
    file_id = _identifier(_get_str(data, "id", where), where)
    where = f"{where} ({file_id})"

    def strings(key: str) -> list[str]:
        values = _require_list(data.get(key), f"{where} {key}")
        if not all(isinstance(value, str) for value in values):
            raise ContractError(f"{where}: '{key}' must list strings")
        return [str(value) for value in values]

    return ContractFile(
        id=file_id,
        cpp=_get_str(data, "cpp", where),
        hlsl=_get_str(data, "hlsl", where),
        namespace=_get_str(data, "namespace", where),
        cpp_includes=strings("cpp_includes"),
        hlsl_includes=strings("hlsl_includes"),
        constants=[
            _parse_constant(item, f"{where} constant {index}")
            for index, item in enumerate(
                _require_list(data.get("constants"), f"{where} constants")
            )
        ],
        structs=[
            _parse_struct(item, f"{where} struct {index}")
            for index, item in enumerate(
                _require_list(data.get("structs"), f"{where} structs")
            )
        ],
    )


def domain_tags(bindless: Mapping[str, object]) -> dict[str, str]:
    """Map each bindless domain id to the tag of its `BX_IN_<TAG>` guard."""
    abi = _require_mapping(bindless.get("abi"), "bindless abi")
    tags: dict[str, str] = {}
    for index, raw in enumerate(_require_list(abi.get("domains"), "domains")):
        domain = _require_mapping(raw, f"bindless domain {index}")
        tags[_get_str(domain, "id", "bindless domain")] = upper_snake(
            _get_str(domain, "name", "bindless domain")
        )
    return tags


def parse_spec(document: object, bindless: object, source: str = "<memory>") -> Spec:
    """Parse and validate a contract schema against the bindless ABI."""
    data = _require_mapping(document, "contracts")
    _check_keys(data, {"meta", "files"}, "contracts")
    spec = Spec(
        source=source,
        files=[
            _parse_file(item, f"file {index}")
            for index, item in enumerate(_require_list(data.get("files"), "files"))
        ],
        domain_tags=domain_tags(_require_mapping(bindless, "bindless")),
    )
    _validate(spec)
    return spec


# --- Layout ------------------------------------------------------------------


def _field_type_size_align(spec: Spec, item: Field) -> tuple[int, int]:
    builtin = BUILTIN_TYPES.get(item.type)
    if builtin is not None:
        return builtin.size, builtin.align
    nested = spec.structs[item.type]
    return nested.size, max(nested.align, _natural_align(spec, nested))


def _natural_align(spec: Spec, struct: Struct) -> int:
    return max(_field_type_size_align(spec, item)[1] for item in struct.fields)


def _layout_struct(spec: Spec, struct: Struct, where: str) -> None:
    offset = 0
    struct.offsets = []
    for item in struct.fields:
        size, align = _field_type_size_align(spec, item)
        builtin = BUILTIN_TYPES.get(item.type)
        if offset % align != 0:
            raise ContractError(
                f"{where}: '{item.name}' at offset {offset} is not aligned to "
                f"{align} bytes; add explicit padding"
            )
        if struct.layout is Layout.CONSTANT:
            starts_register = item.count != 0 or builtin is None or builtin.is_matrix
            if starts_register and offset % _CBUFFER_REGISTER_BYTES != 0:
                raise ContractError(
                    f"{where}: '{item.name}' must start a 16-byte register in "
                    "a constant layout; add explicit padding"
                )
            if item.count != 0 and size % _CBUFFER_REGISTER_BYTES != 0:
                raise ContractError(
                    f"{where}: array '{item.name}' elements must fill 16-byte "
                    "registers in a constant layout"
                )
            first = offset // _CBUFFER_REGISTER_BYTES
            last = (offset + size - 1) // _CBUFFER_REGISTER_BYTES
            if not starts_register and first != last:
                raise ContractError(
                    f"{where}: '{item.name}' straddles a 16-byte register; "
                    "add explicit padding"
                )
        loadable = (
            spec.structs[item.type].layout is Layout.BYTES
            if builtin is None
            else not builtin.is_matrix and builtin.scalar != "uint64"
        )
        if struct.layout is Layout.BYTES and not loadable:
            raise ContractError(
                f"{where}: '{item.name}' cannot be loaded from a byte-address "
                "buffer; use 32-bit scalars, vectors or byte-layout structs"
            )
        struct.offsets.append(offset)
        offset += size * max(item.count, 1)
    if offset != struct.size:
        raise ContractError(
            f"{where}: fields occupy {offset} bytes, the contract declares "
            f"{struct.size}"
        )
    if struct.size % struct.align != 0:
        raise ContractError(
            f"{where}: size {struct.size} is not a multiple of its alignment "
            f"{struct.align}"
        )


def _validate(spec: Spec) -> None:
    cpp_paths: set[str] = set()
    for contract_file in spec.files:
        for path in (contract_file.cpp, contract_file.hlsl):
            if path in cpp_paths:
                raise ContractError(f"output '{path}' is listed twice")
            cpp_paths.add(path)
        for struct in contract_file.structs:
            if struct.name in spec.structs:
                raise ContractError(f"struct '{struct.name}' is defined twice")
            where = f"{contract_file.id}.{struct.name}"
            for item in struct.fields:
                if item.type not in BUILTIN_TYPES and item.type not in spec.structs:
                    raise ContractError(
                        f"{where}: '{item.name}' has unknown type '{item.type}' "
                        "(nested structs must be defined first)"
                    )
                if item.slot:
                    if item.slot != ANY_DOMAIN and item.slot not in spec.domain_tags:
                        raise ContractError(
                            f"{where}: '{item.name}' indexes unknown domain "
                            f"'{item.slot}'"
                        )
                    if item.type != "uint":
                        raise ContractError(
                            f"{where}: slot '{item.name}' must be a uint"
                        )
            if struct.loader is not None and (
                struct.loader.domain not in spec.domain_tags
            ):
                raise ContractError(
                    f"{where}: loader reads unknown domain '{struct.loader.domain}'"
                )
            if struct.register and struct.layout is not Layout.CONSTANT:
                raise ContractError(f"{where}: only constant layouts bind b#")
            _layout_struct(spec, struct, where)
            spec.structs[struct.name] = struct


# --- C++ rendering -----------------------------------------------------------


def _doc_lines(doc: str, prefix: str) -> list[str]:
    return [f"{prefix} {line}".rstrip() for line in doc.splitlines()]


def _cpp_field_type(spec: Spec, item: Field) -> str:
    if item.cpp:
        element = item.cpp
    elif item.slot:
        element = "ShaderVisibleIndex"
    elif item.type in BUILTIN_TYPES:
        element = BUILTIN_TYPES[item.type].cpp
    else:
        element = spec.structs[item.type].name
    if item.count:
        return f"std::array<{element}, {item.count}>"
    return element


def _cpp_field_default(item: Field) -> str:
    if item.default:
        return f" {{ {item.default} }}"
    if item.count or item.cpp or item.type not in BUILTIN_TYPES:
        return " {}"
    if item.slot:
        return " { kInvalidShaderVisibleIndex }"
    return f" {{ {BUILTIN_TYPES[item.type].cpp_default()} }}"


def _cpp_constant(constant: Constant) -> list[str]:
    if constant.type == "float":
        cpp_type, literal = "float", f"{float(constant.value)!r}F"
    elif constant.type == "int":
        cpp_type, literal = "std::int32_t", str(constant.value)
    else:
        cpp_type, literal = "std::uint32_t", f"{constant.value}U"
    return [
        *_doc_lines(constant.doc, "//!"),
        f"inline constexpr {cpp_type} {constant.cpp} = {literal};",
    ]


def _cpp_includes(spec: Spec, contract_file: ContractFile) -> list[str]:
    standard = {"<cstddef>", "<type_traits>"}
    project: set[str] = set()
    for constant in contract_file.constants:
        if constant.type != "float":
            standard.add("<cstdint>")
    for struct in contract_file.structs:
        for item in struct.fields:
            if item.count or "std::array" in item.cpp:
                standard.add("<array>")
            builtin = BUILTIN_TYPES.get(item.type)
            if item.cpp:
                continue
            if item.slot:
                project.add("<Oxygen/Core/Bindless/Types.h>")
            elif builtin is not None and builtin.cpp_include:
                (project if "glm" in builtin.cpp_include else standard).add(
                    builtin.cpp_include
                )
    for include in contract_file.cpp_includes:
        project.add(include)
    for struct in contract_file.structs:
        for item in struct.fields:
            nested = spec.structs.get(item.type)
            if nested is not None and nested not in contract_file.structs:
                owner = next(other for other in spec.files if nested in other.structs)
                project.add(f"<{owner.cpp}>")
    lines = [f"#include {include}" for include in sorted(standard)]
    glm = sorted(include for include in project if include.startswith("<glm/"))
    oxygen = sorted(include for include in project if include not in glm)
    for group in (glm, oxygen):
        if group:
            lines.append("")
            lines.extend(f"#include {include}" for include in group)
    return lines


def render_cpp(spec: Spec, contract_file: ContractFile) -> str:
    """The C++ header of one contract file, before clang-format."""
    lines = [
        _LICENSE,
        f"// Generated by BindlessCodeGen from {spec.source}; do not edit.",
        "",
        "#pragma once",
        "",
        *_cpp_includes(spec, contract_file),
        "",
        f"namespace {contract_file.namespace} {{",
        "",
    ]
    for constant in contract_file.constants:
        lines.extend(_cpp_constant(constant))
    if contract_file.constants:
        lines.append("")
    for struct in contract_file.structs:
        lines.extend(_doc_lines(struct.doc, "//!"))
        alignas = f"alignas({struct.align}) " if struct.align > 4 else ""
        lines.append(f"struct {alignas}{struct.name} {{")
        for item in struct.fields:
            lines.extend(_doc_lines(item.doc, "  //!"))
            lines.append(
                f"  {_cpp_field_type(spec, item)} {item.name}"
                f"{_cpp_field_default(item)};"
            )
        lines.append("};")
        lines.append("")
        lines.append(f"static_assert(std::is_standard_layout_v<{struct.name}>);")
        lines.append(f"static_assert(std::is_trivially_copyable_v<{struct.name}>);")
        lines.append(f"static_assert(sizeof({struct.name}) == {struct.size}U);")
        if struct.align > 4:
            lines.append(f"static_assert(alignof({struct.name}) == {struct.align}U);")
        for item, offset in zip(struct.fields, struct.offsets):
            lines.append(
                f"static_assert(offsetof({struct.name}, {item.name}) == {offset}U);"
            )
        lines.append("")
    lines.append(f"}} // namespace {contract_file.namespace}")
    return "\n".join(lines) + "\n"


# --- HLSL rendering ----------------------------------------------------------


def _has_slots(spec: Spec, struct: Struct) -> bool:
    return any(
        item.slot
        or (item.type in spec.structs and _has_slots(spec, spec.structs[item.type]))
        for item in struct.fields
    )


def _hlsl_type(spec: Spec, item: Field) -> str:
    if item.type in BUILTIN_TYPES:
        return item.type
    return spec.structs[item.type].hlsl_name


def _hlsl_constant(constant: Constant) -> list[str]:
    if constant.type == "float":
        literal = f"{float(constant.value)!r}f"
    elif constant.type == "int":
        literal = str(constant.value)
    else:
        literal = f"{constant.value}u"
    return [
        *_doc_lines(constant.doc, "//"),
        f"static const {constant.type} {constant.hlsl} = {literal};",
    ]


def _hlsl_members(spec: Spec, struct: Struct) -> list[str]:
    lines: list[str] = []
    for item in struct.fields:
        lines.extend(_doc_lines(item.doc, "    //"))
        suffix = f"[{item.count}]" if item.count else ""
        lines.append(f"    {_hlsl_type(spec, item)} {item.name}{suffix};")
    return lines


def _hlsl_invalid(spec: Spec, struct: Struct) -> list[str]:
    name = struct.hlsl_name
    lines = [
        f"// A {name} whose bindless slots are all invalid.",
        f"static {name} MakeInvalid{name}()",
        "{",
        f"    {name} value = ({name})0;",
    ]
    for item in struct.fields:
        nested = spec.structs.get(item.type)
        if item.slot:
            value = "K_INVALID_BINDLESS_INDEX"
        elif nested is not None and _has_slots(spec, nested):
            value = f"MakeInvalid{nested.hlsl_name}()"
        else:
            continue
        if item.count:
            lines.extend(
                f"    value.{item.name}[{index}] = {value};"
                for index in range(item.count)
            )
        else:
            lines.append(f"    value.{item.name} = {value};")
    lines.extend(["    return value;", "}"])
    return lines


def _hlsl_loader(spec: Spec, struct: Struct, loader: Loader) -> list[str]:
    name = struct.hlsl_name
    tag = spec.domain_tags[loader.domain]
    initial = f"MakeInvalid{name}()" if _has_slots(spec, struct) else f"({name})0"
    index = ", uint index" if loader.array else ""
    what = "element `index` of" if loader.array else "the record at"
    lines = [
        f"// Loads {what} `slot`; false, with `value` invalid, when `slot` is",
        f"// outside the {loader.domain} domain"
        + (" or `index` is out of range." if loader.array else "."),
        f"static bool TryLoad{name}(uint slot{index}, out {name} value)",
        "{",
        f"    value = {initial};",
        f"    if (!BX_IN_{tag}(slot)) {{",
        "        return false;",
        "    }",
        f"    StructuredBuffer<{name}> buffer = ResourceDescriptorHeap[slot];",
    ]
    if loader.array:
        lines.extend(
            [
                "    uint count;",
                "    uint stride;",
                "    buffer.GetDimensions(count, stride);",
                "    if (index >= count) {",
                "        return false;",
                "    }",
                "    value = buffer[index];",
            ]
        )
    else:
        lines.append("    value = buffer[0];")
    lines.extend(["    return true;", "}"])
    return lines


def _bytes_prefix(struct: Struct) -> str:
    return struct.hlsl_prefix or upper_snake(struct.hlsl_name)


def _bytes_offset_lines(
    spec: Spec, struct: Struct, prefix: str, base: int
) -> list[str]:
    """Offsets of every member of `struct` at byte `base`; members of nested
    records that are not arrays are flattened into absolute offsets."""
    lines: list[str] = []
    for item, offset in zip(struct.fields, struct.offsets):
        name = f"{prefix}_{item.name.upper()}"
        lines.append(f"static const uint {name}_OFFSET = {base + offset}u;")
        if item.count:
            stride = _field_type_size_align(spec, item)[0]
            lines.append(f"static const uint {name}_STRIDE = {stride}u;")
            continue
        nested = spec.structs.get(item.type)
        if nested is not None:
            lines.extend(_bytes_offset_lines(spec, nested, name, base + offset))
    return lines


def _hlsl_bytes_offsets(spec: Spec, struct: Struct) -> list[str]:
    prefix = _bytes_prefix(struct)
    return [
        f"// Byte offsets of {struct.hlsl_name} in a byte-address buffer.",
        f"static const uint {prefix}_SIZE = {struct.size}u;",
        *_bytes_offset_lines(spec, struct, prefix, 0),
    ]


def _bytes_element_access(spec: Spec, item: Field) -> tuple[str, str]:
    """The load expression and store value of one element of `item`, with
    `{address}` and `{value}` placeholders."""
    builtin = BUILTIN_TYPES.get(item.type)
    if builtin is None:
        nested = spec.structs[item.type].hlsl_name
        return (
            f"Load{nested}(buffer, {{address}})",
            f"Store{nested}(buffer, {{address}}, {{value}})",
        )
    load = "Load" if builtin.components == 1 else f"Load{builtin.components}"
    store = "Store" if builtin.components == 1 else f"Store{builtin.components}"
    raw = f"buffer.{load}({{address}})"
    if builtin.scalar == "float":
        return f"asfloat({raw})", f"buffer.{store}({{address}}, asuint({{value}}))"
    if builtin.scalar == "int":
        return f"asint({raw})", f"buffer.{store}({{address}}, asuint({{value}}))"
    return raw, f"buffer.{store}({{address}}, {{value}})"


def _hlsl_bytes_access(spec: Spec, struct: Struct) -> list[str]:
    name = struct.hlsl_name
    prefix = _bytes_prefix(struct)
    lines: list[str] = []
    for buffer_type in ("ByteAddressBuffer", "RWByteAddressBuffer"):
        lines.extend(
            [
                f"static {name} Load{name}({buffer_type} buffer, uint base)",
                "{",
                f"    {name} value;",
            ]
        )
        for item in struct.fields:
            load, _ = _bytes_element_access(spec, item)
            offset = f"base + {prefix}_{item.name.upper()}_OFFSET"
            if item.count:
                stride = f"{prefix}_{item.name.upper()}_STRIDE"
                for index in range(item.count):
                    address = f"{offset} + {index}u * {stride}"
                    lines.append(
                        f"    value.{item.name}[{index}] = "
                        f"{load.format(address=address)};"
                    )
            else:
                lines.append(f"    value.{item.name} = {load.format(address=offset)};")
        lines.extend(["    return value;", "}", ""])
    lines.extend(
        [
            f"static void Store{name}(RWByteAddressBuffer buffer, uint base, "
            f"{name} value)",
            "{",
        ]
    )
    for item in struct.fields:
        _, store = _bytes_element_access(spec, item)
        offset = f"base + {prefix}_{item.name.upper()}_OFFSET"
        if item.count:
            stride = f"{prefix}_{item.name.upper()}_STRIDE"
            for index in range(item.count):
                address = f"{offset} + {index}u * {stride}"
                lines.append(
                    "    "
                    + store.format(address=address, value=f"value.{item.name}[{index}]")
                    + ";"
                )
        else:
            lines.append(
                "    " + store.format(address=offset, value=f"value.{item.name}") + ";"
            )
    lines.append("}")
    return lines


def _include_guard(path: str) -> str:
    return "OXYGEN_GENERATED_" + re.sub(r"[^A-Za-z0-9]", "_", path).upper()


def render_hlsl(spec: Spec, contract_file: ContractFile) -> str:
    """The HLSL header of one contract file."""
    guard = _include_guard(contract_file.hlsl)
    includes = list(contract_file.hlsl_includes)
    if any(
        _has_slots(spec, struct) or struct.loader is not None
        for struct in contract_file.structs
    ):
        includes.insert(0, "Core/Bindless/Generated.BindlessAbi.hlsl")
    for struct in contract_file.structs:
        for item in struct.fields:
            nested = spec.structs.get(item.type)
            if nested is not None and nested not in contract_file.structs:
                owner = next(other for other in spec.files if nested in other.structs)
                includes.append(owner.hlsl)
    lines = [
        _LICENSE,
        f"// Generated by BindlessCodeGen from {spec.source}; do not edit.",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]
    seen: set[str] = set()
    for include in includes:
        if include not in seen:
            seen.add(include)
            lines.append(f'#include "{include}"')
    if includes:
        lines.append("")
    for constant in contract_file.constants:
        lines.extend(_hlsl_constant(constant))
    if contract_file.constants:
        lines.append("")
    for struct in contract_file.structs:
        lines.extend(_doc_lines(struct.doc, "//"))
        if struct.register:
            lines.append(
                f"cbuffer {struct.hlsl_name} : register({struct.register}, space0)"
            )
            lines.extend(["{", *_hlsl_members(spec, struct), "}", ""])
            continue
        lines.extend(
            [f"struct {struct.hlsl_name}", "{", *_hlsl_members(spec, struct), "};", ""]
        )
        if _has_slots(spec, struct):
            lines.extend([*_hlsl_invalid(spec, struct), ""])
        if struct.loader is not None:
            lines.extend([*_hlsl_loader(spec, struct, struct.loader), ""])
        if struct.layout is Layout.BYTES:
            lines.extend([*_hlsl_bytes_offsets(spec, struct), ""])
            lines.extend([*_hlsl_bytes_access(spec, struct), ""])
    lines.append(f"#endif // {guard}")
    return "\n".join(lines) + "\n"


# --- Driver ------------------------------------------------------------------


def _load_yaml(path: Path) -> object:
    with path.open("r", encoding="utf-8") as stream:
        return yaml.safe_load(stream)


def render_all(
    spec: Spec, cpp_root: Path, hlsl_root: Path, *, format_cpp_files: bool = True
) -> dict[Path, str]:
    """Every generated file of `spec`, by output path."""
    outputs: dict[Path, str] = {}
    for contract_file in spec.files:
        cpp_path = cpp_root / contract_file.cpp
        cpp = render_cpp(spec, contract_file)
        if format_cpp_files:
            cpp = format_cpp(cpp, str(cpp_path))
        outputs[cpp_path] = cpp
        outputs[hlsl_root / contract_file.hlsl] = render_hlsl(spec, contract_file)
    return outputs


def generate(
    spec_path: Path,
    bindless_path: Path,
    cpp_root: Path,
    hlsl_root: Path,
    *,
    check: bool = False,
) -> list[Path]:
    """Generate the contract files; returns the paths that changed (or, with
    `check`, that would change)."""
    source = spec_path.resolve()
    engine_root = cpp_root.resolve().parent
    try:
        label = source.relative_to(engine_root).as_posix()
    except ValueError:
        label = source.name
    spec = parse_spec(_load_yaml(spec_path), _load_yaml(bindless_path), label)
    changed: list[Path] = []
    for path, content in render_all(spec, cpp_root, hlsl_root).items():
        if check:
            current = path.read_bytes() if path.is_file() else b""
            if current != content.encode("utf-8"):
                changed.append(path)
        elif write_if_changed(path, content):
            changed.append(path)
    return changed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="bindless_codegen.contracts")
    parser.add_argument("--spec", required=True, type=Path)
    parser.add_argument("--bindless", required=True, type=Path)
    parser.add_argument(
        "--cpp-root",
        required=True,
        type=Path,
        help="Root of C++ include paths (the engine src directory)",
    )
    parser.add_argument(
        "--hlsl-root",
        required=True,
        type=Path,
        help="Root of shader include paths",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail when a generated file is missing or stale",
    )
    args = parser.parse_args(argv)
    spec_path: Path = args.spec
    bindless_path: Path = args.bindless
    cpp_root: Path = args.cpp_root
    hlsl_root: Path = args.hlsl_root
    check: bool = args.check
    try:
        changed = generate(spec_path, bindless_path, cpp_root, hlsl_root, check=check)
    except ContractError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    for path in changed:
        verb = "stale" if check else "wrote"
        print(f"[ShaderContracts] {verb} {path.as_posix()}")
    return 1 if check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
