# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Domain rendering and validations."""

from __future__ import annotations

from typing import Any, Dict, List
import re


def _normalize_acronyms(name: str) -> str:
    """Convert all-all-caps runs (2+ letters) to Camel form: SRV -> Srv."""
    return re.sub(
        r"([A-Z]{2,})(?![a-z])",
        lambda m: m.group(0)[0] + m.group(0)[1:].lower(),
        name,
    )


def render_cpp_domains(domains: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    for d in domains:
        name = d.get("name")
        base = d.get("domain_base")
        cap = d.get("capacity")
        comment = d.get("comment", "")
        if base is not None:
            if comment:
                lines.append(f"  // {comment}")
                lines.append(f"")
            # C++ constants use kUpperCamelCase
            cpp_name = _normalize_acronyms(str(name)) if name else ""
            lines.append(
                f"  static constexpr uint32_t k{ cpp_name }DomainBase = {int(base)}U;"
            )
            if cap is not None:
                lines.append(
                    f"  static constexpr uint32_t k{ cpp_name }Capacity = {int(cap)}U;"
                )
            lines.append("")
    return "\n".join(lines)


def render_hlsl_domains(domains: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    tags: List[str] = []
    for d in domains:
        name = d.get("name")
        base = d.get("domain_base")
        cap = d.get("capacity")
        comment = d.get("comment", "")

        # Convert display name like "GlobalSRV" to UPPER_SNAKE_CASE like "GLOBAL_SRV"
        def _upper_snake(n: str) -> str:
            if not n:
                return ""
            # First, normalize acronyms so SRV -> Srv, UAV -> Uav, etc.
            camel = _normalize_acronyms(str(n))
            # Robustly insert underscores at word boundaries:
            # 1) between lowercase/digit and uppercase (e.g., GlobalSrv -> Global_Srv)
            s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", camel)
            # 2) between acronym runs and following Camel word (e.g., RGBColor -> RGB_Color)
            s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
            return s.upper()

        up = _upper_snake(str(name)) if name else ""
        if base is not None:
            if comment:
                lines.append(f"// {comment}")
            # HLSL constants use K_UPPER_SNAKE_CASE
            lines.append(f"static const uint K_{up}_DOMAIN_BASE = {int(base)};")
            if cap is not None:
                lines.append(f"static const uint K_{up}_CAPACITY = {int(cap)};")
            lines.append("")
            # Record tag for macro emission if both base and cap are present
            if name is not None and cap is not None:
                tags.append(up)
    # Emit generic macros for domain validation using generated constants
    if tags:
        lines.append("// Domain guard macros (generated)")
        lines.append("#define BX_DOMAIN_BASE(TAG)   K_##TAG##_DOMAIN_BASE")
        lines.append("#define BX_DOMAIN_CAP(TAG)    K_##TAG##_CAPACITY")
        lines.append(
            "#define BX_IN(TAG, IDX)       BX_IsInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))"
        )
        lines.append(
            "#define BX_TRY(TAG, IDX)      BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_BASE(TAG), BX_DOMAIN_CAP(TAG))"
        )
        lines.append("")
        # Also emit per-domain convenience wrappers
        for tag in tags:
            lines.append(f"#define BX_DOMAIN_{tag}_BASE K_{tag}_DOMAIN_BASE")
            lines.append(f"#define BX_DOMAIN_{tag}_CAPACITY K_{tag}_CAPACITY")
            lines.append(
                f"#define BX_IN_{tag}(IDX)  BX_IsInDomain((IDX), BX_DOMAIN_{tag}_BASE, BX_DOMAIN_{tag}_CAPACITY)"
            )
            lines.append(
                f"#define BX_TRY_{tag}(IDX) BX_TryUseGlobalIndexInDomain((IDX), BX_DOMAIN_{tag}_BASE, BX_DOMAIN_{tag}_CAPACITY)"
            )
            lines.append("")
    return "\n".join(lines)


def validate_root_table_references(
    domains: List[Dict[str, Any]], root_sig: List[Dict[str, Any]]
):
    params = {p.get("name"): p for p in root_sig}
    for d in domains:
        rt = d.get("root_table")
        if d.get("kind") in ("SRV", "UAV", "SAMPLER") and rt is None:
            raise ValueError(
                f"Domain '{d.get('id')}' of kind '{d.get('kind')}' must specify 'root_table'"
            )
        if rt is None:
            continue
        if rt not in params:
            raise ValueError(
                f"Domain '{d.get('id')}' references unknown root_signature parameter '{rt}'"
            )
        if (
            d.get("kind") in ("SRV", "UAV", "SAMPLER")
            and params[rt].get("type") != "descriptor_table"
        ):
            raise ValueError(
                f"Domain '{d.get('id')}' must reference a descriptor_table but '{rt}' is type '{params[rt].get('type')}'"
            )


def validate_domain_overlaps_per_root_table(domains: List[Dict[str, Any]]):
    by_rt: Dict[str, List[Dict[str, Any]]] = {}
    for d in domains:
        rt = d.get("root_table")
        if rt is None:
            # non-descriptor domains are ignored here
            continue
        by_rt.setdefault(rt, []).append(d)

    for rt, dlist in by_rt.items():
        intervals = []
        for d in dlist:
            base = d.get("domain_base")
            cap = d.get("capacity")
            if base is None or cap is None:
                raise ValueError(
                    f"Domain '{d.get('id')}' in root_table '{rt}' must specify 'domain_base' and 'capacity'"
                )
            start = int(base)
            end = start + int(cap)
            intervals.append((start, end, d.get("id")))
        intervals.sort(key=lambda t: t[0])
        for i in range(1, len(intervals)):
            prev = intervals[i - 1]
            cur = intervals[i]
            if cur[0] < prev[1]:
                raise ValueError(
                    f"Domain address ranges overlap in root_table '{rt}': '{prev[2]}' [{prev[0]},{prev[1]}) overlaps '{cur[2]}' [{cur[0]},{cur[1]})"
                )
