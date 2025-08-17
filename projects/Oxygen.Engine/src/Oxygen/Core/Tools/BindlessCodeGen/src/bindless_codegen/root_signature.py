# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Root signature specific validations and helpers."""

from __future__ import annotations

from typing import Any, Dict, List


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
