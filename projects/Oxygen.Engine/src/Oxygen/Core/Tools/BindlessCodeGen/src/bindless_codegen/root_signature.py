# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Root signature specific validations and helpers."""

from __future__ import annotations

from typing import Any, Dict, List
from textwrap import indent

from . import domains as domains_mod


def _digits(s: str) -> int | None:
    try:
        return int("".join([c for c in s if c.isdigit()]))
    except Exception:
        return None


def validate_params_vs_domains(
    root_sig: List[Dict[str, Any]], domains_by_id: Dict[str, Dict[str, Any]]
):
    """Validate root parameters and ranges consistency with domains.

    - range_type vs domain.kind
    - base_shader_register/register_space match domain register/space (first domain when multi-domain)
    - UAV + counters cannot be referenced by unbounded ranges
    - CBV array sizes vs domain capacity (when mapped)
    """
    for idx, param in enumerate(root_sig):
        if param.get("type") == "descriptor_table":
            param_domains = param.get("domains") or []
            ranges = param.get("ranges", []) or []
            for r_i, r in enumerate(ranges):
                doms = list(param_domains)
                if r.get("domain"):
                    doms = list(r.get("domain")) + doms
                if not doms:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} must specify 'domain'"
                    )
                rt = r.get("range_type")
                if rt is None:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} missing required 'range_type'"
                    )
                if r.get("num_descriptors") is None:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} missing required 'num_descriptors'"
                    )
                first = doms[0]
                for dom_id in doms:
                    d = domains_by_id.get(dom_id)
                    if d is None:
                        raise ValueError(
                            f"root_signature parameter {idx} range {r_i} references unknown domain '{dom_id}'"
                        )
                    kind = d.get("kind")
                    if kind is not None and rt is not None and kind != rt:
                        raise ValueError(
                            f"root_signature parameter {idx} range {r_i} range_type '{rt}' does not match domain '{dom_id}' kind '{kind}'"
                        )
                    # register match only enforced for first domain when multi-domain
                    dom_reg = d.get("register")
                    if dom_reg is not None and dom_id == first:
                        base = r.get("base_shader_register")
                        if base is not None:
                            reg_num = _digits(str(dom_reg))
                            base_num = (
                                _digits(str(base))
                                if isinstance(base, str)
                                else int(base)
                            )
                            if (
                                reg_num is not None
                                and base_num is not None
                                and reg_num != int(base_num)
                            ):
                                raise ValueError(
                                    f"root_signature parameter {idx} range {r_i} base_shader_register {base} does not match domain '{dom_id}' register {dom_reg}"
                                )
                    dom_space = d.get("space")
                    if dom_space is not None:
                        space = r.get("register_space")
                        if space is not None:
                            dom_space_num = (
                                _digits(str(dom_space))
                                if isinstance(dom_space, str)
                                else int(dom_space)
                            )
                            space_num = (
                                _digits(str(space))
                                if isinstance(space, str)
                                else int(space)
                            )
                            if (
                                dom_space_num is not None
                                and space_num is not None
                                and int(space_num) != int(dom_space_num)
                            ):
                                raise ValueError(
                                    f"root_signature parameter {idx} range {r_i} register_space {space} does not match domain '{dom_id}' space {dom_space}"
                                )

    # UAV + counters cannot be referenced by unbounded ranges
    for d in domains_by_id.values():
        if d.get("kind") == "UAV" and d.get("uav_counter_register") is not None:
            for idx, param in enumerate(root_sig):
                if param.get("type") != "descriptor_table":
                    continue
                param_domains = param.get("domains") or []
                for r_i, r in enumerate(param.get("ranges", []) or []):
                    doms = list(param_domains)
                    if r.get("domain"):
                        doms = list(r.get("domain")) + doms
                    if d.get("id") in doms:
                        nd = r.get("num_descriptors")
                        if isinstance(nd, str) and nd == "unbounded":
                            raise ValueError(
                                f"Descriptor_table parameter {idx} range {r_i} references UAV domain '{d.get('id')}' which cannot be 'unbounded' when a UAV counter is present"
                            )

    # CBV array sizes vs domain capacity (when mapped)
    for idx, param in enumerate(root_sig):
        if param.get("type") == "cbv":
            arr_size = param.get("cbv_array_size")
            if arr_size is not None:
                name = param.get("name")
                mapped = [
                    d
                    for d in domains_by_id.values()
                    if d.get("root_table") == name
                ]
                for d in mapped:
                    cap = d.get("capacity")
                    if cap is not None and int(arr_size) > int(cap):
                        raise ValueError(
                            f"CBV parameter '{name}' cbv_array_size {arr_size} exceeds domain '{d.get('id')}' capacity {cap}"
                        )


def render_cpp_root_signature(root_sig: List[Dict[str, Any]]) -> Dict[str, str]:
    """Render richer C++ metadata for the root signature.

    Returns a dict with keys:
      - root_param_enums: enum entries
      - root_constants_counts: counts lines
      - register_space_consts: register/space consts
      - root_param_structs: struct definitions and per-param ranges arrays
      - root_param_table: constexpr RootParamDesc table
    """
    enum_lines: List[str] = []
    counts_lines: List[str] = []
    regs_lines: List[str] = []
    structs_lines: List[str] = []
    table_entries: List[str] = []

    def _norm(name: str) -> str:
        return domains_mod._normalize_acronyms(name)

    # Helper to parse register/space tokens like 't0','b1','space0'
    def _num(s):
        if s is None:
            return 0
        if isinstance(s, int):
            return int(s)
        d = _digits(str(s))
        return int(d) if d is not None else 0

    # Range type mapping (emit as enum class RangeType)
    RANGE_MAP = {
        "SRV": "RangeType::SRV",
        "SAMPLER": "RangeType::Sampler",
        "UAV": "RangeType::UAV",
    }

    for idx, p in enumerate(root_sig or []):
        name = p.get("name") or f"Param{idx}"
        cname = _norm(str(name))
        enum_lines.append(f"  k{cname} = {idx},")

        # counts for root_constants
        if p.get("type") == "root_constants":
            n = int(p.get("num_32bit_values", 0))
            counts_lines.append(
                f"static constexpr uint32_t k{cname}ConstantsCount = {n}u;"
            )

        # register/space shortcuts
        reg = p.get("shader_register") or p.get("base_shader_register")
        space = p.get("register_space")
        if reg is not None:
            sreg = str(reg)
            digits = "".join([c for c in sreg if c.isdigit()]) or "0"
            regs_lines.append(
                f"static constexpr uint32_t k{cname}Register = {int(digits)}u; // '{sreg}'"
            )
        if space is not None:
            sspace = str(space)
            digits = "".join([c for c in sspace if c.isdigit()]) or "0"
            regs_lines.append(
                f"static constexpr uint32_t k{cname}Space = {int(digits)}u; // '{sspace}'"
            )

        # Build per-parameter ranges arrays for descriptor_table
        if p.get("type") == "descriptor_table":
            ranges = p.get("ranges", []) or []
            ranges_name = f"kRootParam{idx}Ranges"
            range_lines: List[str] = []
            for r in ranges:
                rt = r.get("range_type")
                rt_v = (
                    RANGE_MAP.get(str(rt).upper(), "RangeType::SRV")
                    if rt is not None
                    else "RangeType::SRV"
                )
                base = _num(r.get("base_shader_register"))
                space_n = _num(r.get("register_space"))
                nd = r.get("num_descriptors")
                if isinstance(nd, str) and nd == "unbounded":
                    nd_v = "std::numeric_limits<uint32_t>::max()"
                else:
                    try:
                        nd_v = f"{int(nd)}u"
                    except Exception:
                        nd_v = "std::numeric_limits<uint32_t>::max()"
                range_lines.append(
                    f"    RootParamRange{{ {rt_v}, {base}u, {space_n}u, {nd_v} }},"
                )
            if not range_lines:
                # Emit a sentinel empty range
                range_lines.append(
                    "    RootParamRange{ RangeType::SRV, 0u, 0u, 0u },"
                )
            # Emit std::array with double-brace init for aggregate
            arr = (
                "static constexpr std::array<RootParamRange, %d> %s = { {\n"
                % (len(range_lines), ranges_name)
            )
            arr += "\n".join(range_lines)
            arr += "\n} };"
            structs_lines.append(arr)
            # Use std::span constructed from data() and size()
            table_entries.append(
                f"  RootParamDesc{{ RootParamKind::DescriptorTable, 0u, 0u, std::span<const RootParamRange>{{ {ranges_name}.data(), {ranges_name}.size() }}, static_cast<uint32_t>({ranges_name}.size()), 0u }},"
            )
        elif p.get("type") == "cbv":
            reg_n = _num(p.get("shader_register"))
            space_n = _num(p.get("register_space"))
            table_entries.append(
                f"  RootParamDesc{{ RootParamKind::CBV, {reg_n}u, {space_n}u, std::span<const RootParamRange>{{}}, 0u, 0u }},"
            )
        elif p.get("type") == "root_constants":
            reg_n = _num(p.get("shader_register"))
            space_n = _num(p.get("register_space"))
            n32 = int(p.get("num_32bit_values", 0))
            table_entries.append(
                f"  RootParamDesc{{ RootParamKind::RootConstants, {reg_n}u, {space_n}u, std::span<const RootParamRange>{{}}, 0u, {n32}u }},"
            )
        else:
            # Unknown type: emit placeholder as empty span
            table_entries.append(
                f"  RootParamDesc{{ RootParamKind::DescriptorTable, 0u, 0u, std::span<const RootParamRange>{{}}, 0u, 0u }},"
            )

    enum_lines.append(f"  kCount = {len(root_sig or [])},")

    # Compose structs header
    structs_header = """
// Root parameter runtime descriptors (C++20 idiomatic)
enum class RootParamKind : uint8_t { DescriptorTable = 0, CBV = 1, RootConstants = 2 };

enum class RangeType : uint8_t { SRV = 0, Sampler = 1, UAV = 2 };

struct RootParamRange {
  RangeType range_type;
  uint32_t base_register;
  uint32_t register_space;
  uint32_t num_descriptors; // std::numeric_limits<uint32_t>::max() == unbounded
};

struct RootParamDesc {
  RootParamKind kind;
  uint32_t shader_register;
  uint32_t register_space;
  std::span<const RootParamRange> ranges; // empty span for non-tables
  uint32_t ranges_count;
  uint32_t constants_count; // for root constants
};
"""

    # Compose ranges and table
    structs_all = [structs_header]
    if structs_lines:
        structs_all.extend(structs_lines)
    structs_all.append(
        "static constexpr std::array<RootParamDesc, "
        + str(len(table_entries))
        + "> kRootParamTable = { {\n"
        + "\n".join(table_entries)
        + "\n} };"
    )

    return {
        "root_param_enums": "\n".join(enum_lines),
        "root_constants_counts": "\n".join(counts_lines) or "// none",
        "register_space_consts": "\n".join(regs_lines) or "// none",
        "root_param_structs": "\n\n".join(structs_all),
        "root_param_table": "static constexpr auto kRootParamTableCount = static_cast<uint32_t>(kRootParamTable.size());",
    }
