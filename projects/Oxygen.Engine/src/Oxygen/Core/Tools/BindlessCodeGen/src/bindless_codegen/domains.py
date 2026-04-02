# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""ABI-domain rendering and validations."""

from __future__ import annotations

from typing import Any, Dict, Iterable, List
import re


_ACCESS_CLASS_TO_VIEW_TYPES: dict[str, set[str]] = {
    "buffer_srv": {
        "TypedBuffer_SRV",
        "StructuredBuffer_SRV",
        "RawBuffer_SRV",
    },
    "buffer_uav": {
        "TypedBuffer_UAV",
        "StructuredBuffer_UAV",
        "RawBuffer_UAV",
    },
    "texture_srv": {"Texture_SRV"},
    "texture_uav": {"Texture_UAV", "SamplerFeedbackTexture_UAV"},
    "constant_buffer": {"ConstantBuffer"},
    "sampler": {"Sampler"},
    "ray_tracing_accel_structure_srv": {"RayTracingAccelStructure"},
}


def _normalize_acronyms(name: str) -> str:
    """Convert all-all-caps runs (2+ letters) to Camel form: SRV -> Srv."""
    return re.sub(
        r"([A-Z]{2,})(?![a-z])",
        lambda m: m.group(0)[0] + m.group(0)[1:].lower(),
        name,
    )


def _upper_snake(name: str) -> str:
    if not name:
        return ""
    camel = _normalize_acronyms(name)
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", camel)
    snake = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", snake)
    return snake.upper()


def _pascal_from_identifier(name: str) -> str:
    if not name:
        return ""
    parts = [part for part in re.split(r"[_\-\s]+", name) if part]
    return "".join(_normalize_acronyms(part[:1].upper() + part[1:]) for part in parts)


def _intervals_overlap(
    intervals: Iterable[tuple[int, int, str]], *, label: str
) -> None:
    ordered = sorted(intervals, key=lambda it: it[0])
    for idx in range(1, len(ordered)):
        prev = ordered[idx - 1]
        cur = ordered[idx]
        if cur[0] < prev[1]:
            raise ValueError(
                f"{label} overlap: '{prev[2]}' [{prev[0]},{prev[1]}) overlaps "
                f"'{cur[2]}' [{cur[0]},{cur[1]})"
            )


def render_cpp_abi(
    index_spaces: List[Dict[str, Any]],
    domains: List[Dict[str, Any]],
) -> Dict[str, str]:
    index_space_constants: List[str] = []
    index_space_entries: List[str] = []
    domain_constants: List[str] = []
    domain_entries: List[str] = []

    for idx, index_space in enumerate(index_spaces):
        index_space_id = str(index_space.get("id"))
        cpp_name = _pascal_from_identifier(index_space_id)
        index_space_constants.append(
            f"inline constexpr IndexSpaceToken k{cpp_name}IndexSpace {{ {idx}U }};"
        )
        index_space_entries.append(
            f'  IndexSpaceDesc{{ k{cpp_name}IndexSpace, "{index_space_id}" }},'
        )

    for idx, domain in enumerate(domains):
        domain_id = str(domain.get("id"))
        domain_name = str(domain.get("name"))
        cpp_name = _normalize_acronyms(domain_name)
        index_space_cpp_name = _pascal_from_identifier(str(domain.get("index_space")))
        shader_index_base = int(domain.get("shader_index_base", 0))
        capacity = int(domain.get("capacity", 0))
        access_class = str(domain.get("shader_access_class"))
        comment = str(domain.get("comment") or "")

        if comment:
            domain_constants.append(f"// {comment}")
        domain_constants.append(
            f"inline constexpr DomainToken k{cpp_name}Domain {{ {idx}U }};"
        )
        domain_constants.append(
            f"inline constexpr uint32_t k{cpp_name}ShaderIndexBase = {shader_index_base}U;"
        )
        domain_constants.append(
            f"inline constexpr uint32_t k{cpp_name}Capacity = {capacity}U;"
        )
        domain_constants.append("")

        domain_entries.append(
            "  DomainDesc{ "
            f"k{cpp_name}Domain, "
            f'"{domain_id}", '
            f'"{domain_name}", '
            f"k{index_space_cpp_name}IndexSpace, "
            f"k{cpp_name}ShaderIndexBase, "
            f"k{cpp_name}Capacity, "
            f'"{access_class}" '
            "},"
        )

    return {
        "index_space_constants": "\n".join(index_space_constants) or "// none",
        "domain_constants": "\n".join(domain_constants).rstrip() or "// none",
        "index_space_entries": "\n".join(index_space_entries) or "  // none",
        "domain_entries": "\n".join(domain_entries) or "  // none",
        "index_space_count": str(len(index_spaces)),
        "domain_count": str(len(domains)),
    }


def render_hlsl_abi(domains: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    tags: List[str] = []
    for domain in domains:
        name = domain.get("name")
        base = domain.get("shader_index_base")
        cap = domain.get("capacity")
        comment = domain.get("comment", "")
        if base is None:
            continue
        tag = _upper_snake(str(name)) if name else ""
        if comment:
            lines.append(f"// {comment}")
            lines.append("")
        lines.append(
            f"static const uint K_{tag}_SHADER_INDEX_BASE = {int(base)};"
        )
        if cap is not None:
            lines.append(f"static const uint K_{tag}_CAPACITY = {int(cap)};")
        lines.append("")
        if cap is not None:
            tags.append(tag)

    if tags:
        lines.append("// Domain guard macros (generated)")
        lines.append("")
        lines.append("#define BX_DOMAIN_BASE(TAG)   K_##TAG##_SHADER_INDEX_BASE")
        lines.append("#define BX_DOMAIN_CAP(TAG)    K_##TAG##_CAPACITY")
        lines.append(
            "#define BX_IN(TAG, IDX)       BX_IsInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))"
        )
        lines.append(
            "#define BX_TRY(TAG, IDX)      BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))"
        )
        lines.append("")
        for tag in tags:
            lines.append(
                f"#define BX_DOMAIN_{tag}_BASE K_{tag}_SHADER_INDEX_BASE"
            )
            lines.append(
                f"#define BX_DOMAIN_{tag}_CAPACITY K_{tag}_CAPACITY"
            )
            lines.append(
                f"#define BX_IN_{tag}(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_{tag}_BASE, BX_DOMAIN_{tag}_CAPACITY)"
            )
            lines.append(
                f"#define BX_TRY_{tag}(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_{tag}_BASE, BX_DOMAIN_{tag}_CAPACITY)"
            )
            lines.append("")

    return "\n".join(lines)


def validate_abi_domains(
    index_spaces: List[Dict[str, Any]], domains: List[Dict[str, Any]]
) -> None:
    index_space_ids: set[str] = set()
    for index_space in index_spaces:
        index_space_id = str(index_space.get("id"))
        if index_space_id in index_space_ids:
            raise ValueError(f"Duplicate ABI index_space id '{index_space_id}'")
        index_space_ids.add(index_space_id)

    ids_seen: set[str] = set()
    names_seen: set[str] = set()
    intervals_by_space: dict[str, List[tuple[int, int, str]]] = {}

    for domain in domains:
        domain_id = str(domain.get("id"))
        domain_name = str(domain.get("name"))
        index_space = str(domain.get("index_space"))
        if domain_id in ids_seen:
            raise ValueError(f"Duplicate ABI domain id '{domain_id}'")
        if domain_name in names_seen:
            raise ValueError(f"Duplicate ABI domain name '{domain_name}'")
        if index_space not in index_space_ids:
            raise ValueError(
                f"ABI domain '{domain_id}' references unknown index_space "
                f"'{index_space}'"
            )
        ids_seen.add(domain_id)
        names_seen.add(domain_name)

        base = int(domain.get("shader_index_base", 0))
        capacity = int(domain.get("capacity", 0))
        if capacity <= 0:
            raise ValueError(
                f"ABI domain '{domain_id}' capacity must be > 0"
            )
        intervals_by_space.setdefault(index_space, []).append(
            (base, base + capacity, domain_id)
        )

        access_class = str(domain.get("shader_access_class"))
        allowed_view_types = _ACCESS_CLASS_TO_VIEW_TYPES.get(access_class)
        if allowed_view_types is None:
            raise ValueError(
                f"ABI domain '{domain_id}' uses unsupported shader_access_class "
                f"'{access_class}'"
            )

        view_types = domain.get("view_types") or []
        if not isinstance(view_types, list) or len(view_types) == 0:
            raise ValueError(
                f"ABI domain '{domain_id}' must declare at least one view_type"
            )

        invalid_view_types = sorted(
            {str(view_type) for view_type in view_types}
            - allowed_view_types
        )
        if invalid_view_types:
            raise ValueError(
                f"ABI domain '{domain_id}' view_types {invalid_view_types} are "
                f"not compatible with shader_access_class '{access_class}'"
            )

    for index_space, intervals in intervals_by_space.items():
        _intervals_overlap(
            intervals,
            label=f"ABI shader-visible range in index_space '{index_space}'",
        )
