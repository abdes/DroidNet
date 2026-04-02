# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""D3D12 root-signature specific validations and helpers."""

from __future__ import annotations

from typing import Any, Dict, List

from . import domains as domains_mod


def _digits(token: Any) -> int | None:
    try:
        return int("".join([c for c in str(token) if c.isdigit()]))
    except Exception:
        return None


def validate_root_signature(
    root_sig: List[Dict[str, Any]], tables_by_id: Dict[str, Dict[str, Any]]
) -> None:
    ids_seen: set[str] = set()
    indices_seen: set[int] = set()

    for idx, param in enumerate(root_sig):
        param_id = str(param.get("id"))
        param_index = int(param.get("index", -1))
        if param_id in ids_seen:
            raise ValueError(
                f"D3D12 root_signature has duplicate id '{param_id}'"
            )
        if param_index in indices_seen:
            raise ValueError(
                f"D3D12 root_signature has duplicate index {param_index}"
            )
        ids_seen.add(param_id)
        indices_seen.add(param_index)

        param_type = str(param.get("type"))
        if param_type == "descriptor_table":
            table_id = str(param.get("table"))
            if table_id not in tables_by_id:
                raise ValueError(
                    f"D3D12 root_signature parameter '{param_id}' references "
                    f"unknown table '{table_id}'"
                )
        elif param_type == "cbv":
            shader_register = str(param.get("shader_register"))
            if not shader_register.startswith("b"):
                raise ValueError(
                    f"D3D12 root_signature parameter '{param_id}' must use a "
                    "CBV register token"
                )
        elif param_type == "root_constants":
            shader_register = str(param.get("shader_register"))
            if not shader_register.startswith("b"):
                raise ValueError(
                    f"D3D12 root_signature parameter '{param_id}' must use a "
                    "CBV register token for root constants"
                )
            if int(param.get("num_32bit_values", 0)) <= 0:
                raise ValueError(
                    f"D3D12 root_signature parameter '{param_id}' must declare "
                    "num_32bit_values > 0"
                )
        else:
            raise ValueError(
                f"D3D12 root_signature parameter {idx} uses unsupported type "
                f"'{param_type}'"
            )


def render_cpp_root_signature(
    root_sig: List[Dict[str, Any]], tables_by_id: Dict[str, Dict[str, Any]]
) -> Dict[str, str]:
    """Render richer C++ metadata for the D3D12 root signature."""
    enum_lines: List[str] = []
    counts_lines: List[str] = []
    regs_lines: List[str] = []
    structs_lines: List[str] = []
    table_entries: List[str] = []

    def _norm(name: str) -> str:
        return domains_mod._normalize_acronyms(name)

    range_map = {
        "SRV": "RangeType::SRV",
        "UAV": "RangeType::UAV",
        "CBV": "RangeType::CBV",
        "SAMPLER": "RangeType::Sampler",
    }

    for idx, param in enumerate(root_sig or []):
        param_id = str(param.get("id") or f"Param{idx}")
        cname = _norm(param_id)
        enum_lines.append(f"  k{cname} = {idx},")

        if param.get("type") == "root_constants":
            count = int(param.get("num_32bit_values", 0))
            counts_lines.append(
                f"static constexpr uint32_t k{cname}ConstantsCount = {count}U;"
            )

        reg = param.get("shader_register")
        space = param.get("register_space")
        if param.get("type") == "descriptor_table":
            table = tables_by_id[str(param.get("table"))]
            reg = table.get("shader_register")
            space = table.get("register_space")

        if reg is not None:
            regs_lines.append(
                f"static constexpr uint32_t k{cname}Register = "
                f"{int(_digits(reg) or 0)}u; // '{reg}'"
            )
        if space is not None:
            regs_lines.append(
                f"static constexpr uint32_t k{cname}Space = "
                f"{int(_digits(space) or 0)}u; // '{space}'"
            )

        if param.get("type") == "descriptor_table":
            table = tables_by_id[str(param.get("table"))]
            descriptor_kind = str(table.get("descriptor_kind"))
            ranges_name = f"kRootParam{idx}Ranges"
            if bool(table.get("unbounded")):
                num_descriptors = "(std::numeric_limits<uint32_t>::max)()"
            else:
                num_descriptors = f"{int(table.get('descriptor_count', 0))}U"
            range_line = (
                "    RootParamRange{ "
                f"{range_map[descriptor_kind]}, "
                f"{int(_digits(table.get('shader_register')) or 0)}U, "
                f"{int(_digits(table.get('register_space')) or 0)}U, "
                f"{num_descriptors} "
                "},"
            )
            structs_lines.append(
                "static constexpr std::array<RootParamRange, 1> "
                f"{ranges_name} = {{ {{\n{range_line}\n}} }};"
            )
            table_entries.append(
                "  RootParamDesc{ RootParamKind::DescriptorTable, 0U, 0U, "
                f"std::span<const RootParamRange>{{ {ranges_name}.data(), "
                f"{ranges_name}.size() }}, "
                f"static_cast<uint32_t>({ranges_name}.size()), 0U }},"
            )
        elif param.get("type") == "cbv":
            table_entries.append(
                "  RootParamDesc{ RootParamKind::CBV, "
                f"{int(_digits(param.get('shader_register')) or 0)}U, "
                f"{int(_digits(param.get('register_space')) or 0)}U, "
                "std::span<const RootParamRange>{}, 0U, 0U },"
            )
        elif param.get("type") == "root_constants":
            table_entries.append(
                "  RootParamDesc{ RootParamKind::RootConstants, "
                f"{int(_digits(param.get('shader_register')) or 0)}U, "
                f"{int(_digits(param.get('register_space')) or 0)}U, "
                "std::span<const RootParamRange>{}, 0U, "
                f"{int(param.get('num_32bit_values', 0))}U }},"
            )

    enum_lines.append(f"  kCount = {len(root_sig or [])},")

    structs_header = """
// Root parameter runtime descriptors (C++20 idiomatic)
enum class RootParamKind : uint8_t { DescriptorTable = 0, CBV = 1, RootConstants = 2 };

enum class RangeType : uint8_t { SRV = 0, Sampler = 1, UAV = 2, CBV = 3 };

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

    structs_all = [structs_header]
    if structs_lines:
        structs_all.extend(structs_lines)
    structs_all.append(
        "static constexpr std::array<RootParamDesc, "
        f"{len(table_entries)}> kRootParamTable = {{ {{\n"
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
