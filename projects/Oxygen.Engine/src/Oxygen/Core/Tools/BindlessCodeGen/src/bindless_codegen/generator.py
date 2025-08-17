# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Generator logic extracted to a module for testing and reuse."""

import yaml
import time
import os
import json
from pathlib import Path
import importlib

try:
    # Normal package import when installed or run as package
    from . import heaps as heaps_module
except Exception:
    # Fallback: load heaps.py directly from same directory when generator is
    # executed as a standalone module via importlib.util.spec_from_file_location
    import importlib.util
    import sys

    heaps_path = Path(__file__).resolve().parent / "heaps.py"
    spec_h = importlib.util.spec_from_file_location(
        "bindless_codegen.heaps", str(heaps_path)
    )
    if spec_h is None or spec_h.loader is None:
        raise ImportError(f"Failed to load heaps module from {heaps_path}")
    heaps_module = importlib.util.module_from_spec(spec_h)
    spec_h.loader.exec_module(heaps_module)
    # register it so other imports can find it by name
    sys.modules["bindless_codegen.heaps"] = heaps_module

# Import jsonschema dynamically to avoid static-analysis errors when the
# package isn't installed in the developer environment.
try:
    jsonschema = importlib.import_module("jsonschema")
except Exception:  # pragma: no cover - availability checked at runtime
    jsonschema = None

# Resolve ValidationError dynamically if jsonschema is present. Use getattr to
# avoid static analyzer errors when jsonschema isn't installed in the environment.
ValidationError = None
if jsonschema is not None:
    val_exceptions = getattr(jsonschema, "exceptions", None)
    if val_exceptions is not None:
        ValidationError = getattr(val_exceptions, "ValidationError", None)

TEMPLATE_CPP = """// Generated file - do not edit.
// Source: {src}
// Generated: {ts}

#ifndef OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H
#define OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H

#include <cstdint>

namespace oxygen {{
namespace engine {{
namespace binding {{

// Invalid sentinel
static constexpr uint32_t kInvalidBindlessIndex = {invalid:#010x}u;

{domain_consts}

}} // namespace binding
}} // namespace engine
}} // namespace oxygen

#endif // OXYGEN_CORE_BINDLESS_BINDINGSLOTS_H
"""

TEMPLATE_HLSL = """// Generated file - do not edit.
// Source: {src}
// Generated: {ts}

#ifndef OXYGEN_BINDING_SLOTS_HLSL
#define OXYGEN_BINDING_SLOTS_HLSL

static const uint K_INVALID_BINDLESS_INDEX = {invalid:#010x};

{domain_defs}

#endif // OXYGEN_BINDING_SLOTS_HLSL
"""


def render_cpp_domains(domains):
    lines = []
    for d in domains:
        name = d.get("name")
        base = d.get("domain_base")
        cap = d.get("capacity")
        comment = d.get("comment", "")
        if base is not None:
            if comment:
                lines.append(f"// {comment}")
            lines.append(
                f"static constexpr uint32_t {name}_DomainBase = {int(base)}u;"
            )
            if cap is not None:
                lines.append(
                    f"static constexpr uint32_t {name}_Capacity = {int(cap)}u;"
                )
            lines.append("")
    return "\n".join(lines)


def render_hlsl_domains(domains):
    lines = []
    for d in domains:
        name = d.get("name")
        base = d.get("domain_base")
        cap = d.get("capacity")
        comment = d.get("comment", "")
        up = name.upper()
        if base is not None:
            if comment:
                lines.append(f"// {comment}")
            lines.append(f"static const uint {up}_DOMAIN_BASE = {int(base)};")
            if cap is not None:
                lines.append(f"static const uint {up}_CAPACITY = {int(cap)};")
            lines.append("")
    return "\n".join(lines)


def atomic_write(path, content):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(content)
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
        if old == content:
            os.remove(tmp)
            return False
    os.replace(tmp, path)
    return True


def validate_input(doc, schema_path=None):
    """Validate a parsed BindingSlots document against the JSON schema.

    If schema_path is None, will look for BindingSlots.schema.json in the
    package root (two levels up from this file).
    """
    if schema_path is None:
        schema_path = (
            Path(__file__).resolve().parents[2] / "BindingSlots.schema.json"
        )
    # If the schema isn't present, skip validation silently.
    if not Path(schema_path).exists():
        return True
    if jsonschema is None:
        raise RuntimeError(
            "The 'jsonschema' package is required to validate BindingSlots.yaml.\n"
            "Install it in your environment: pip install jsonschema"
        )
    try:
        with open(schema_path, "r", encoding="utf-8") as sf:
            schema = json.load(sf)
        jsonschema.validate(instance=doc, schema=schema)
    except Exception as e:
        # jsonschema raises ValidationError on validation failures. We avoid
        # referencing the ValidationError symbol directly in the except clause
        # to keep static analyzers happy when jsonschema is not installed.
        if ValidationError is not None and isinstance(e, ValidationError):
            path_attr = getattr(e, "path", None)
            path = (
                "->".join([str(p) for p in path_attr])
                if path_attr
                else "(root)"
            )
            msg = getattr(e, "message", str(e))
            raise ValueError(
                f"BindingSlots.yaml validation failed at {path}: {msg}"
            ) from e
        raise RuntimeError(
            f"Failed to load/validate schema at {schema_path}: {e}"
        ) from e
    return True


def find_schema(explicit_path: str | None, script_path: str) -> Path | None:
    """Resolve BindingSlots.schema.json according to priority:
    1. explicit_path (if provided)
    2. script_path directory
    3. script_path/../../Bindless/BindingSlots.schema.json

    Returns a Path or None if not found.
    """
    if explicit_path:
        p = Path(explicit_path)
        if p.exists():
            return p
    # look next to the generator script (not cwd)
    script_dir = Path(script_path).resolve().parent
    candidate = script_dir / "BindingSlots.schema.json"
    if candidate.exists():
        return candidate
    # fallback: script_dir/../../Bindless/BindingSlots.schema.json
    candidate2 = script_dir.parents[1] / "Bindless" / "BindingSlots.schema.json"
    if candidate2.exists():
        return candidate2
    return None


def validate_domain_references(doc):
    """Cross-validate that ranges' domain ids reference declared domains and
    that domain->root_table references descriptor table parameter names.

    Raises ValueError on problems so callers can present user-friendly errors.
    """
    domains = doc.get("domains", [])
    domain_by_id = {d.get("id"): d for d in domains}
    root_sig = doc.get("root_signature", [])

    # Enforce uppercase tokens for domain.kind for consistency with root_signature
    allowed_kinds = {"SRV", "CBV", "SAMPLER", "UAV"}
    for d in domains:
        k = d.get("kind")
        if k is None:
            continue
        # normalize/canonicalize string kinds
        if isinstance(k, str) and k.upper() in allowed_kinds:
            d["kind"] = k.upper()
            k = d["kind"]
        if k not in allowed_kinds:
            raise ValueError(
                f"Domain '{d.get('id')}' uses unknown kind '{k}'. Allowed: {sorted(allowed_kinds)}"
            )

    # Validate descriptor_table parameter-level domain references and per-range domains
    for idx, param in enumerate(root_sig):
        ptype = param.get("type")
        # Ensure parameter visibility is present and normalized to D3D12 tokens
        vis = param.get("visibility")
        if vis is None:
            raise ValueError(
                f"root_signature parameter {idx} missing required 'visibility'"
            )
        # normalize single string to list for easier handling
        if isinstance(vis, str):
            vis_list = [vis]
        else:
            vis_list = vis
        # mapping of short aliases to D3D12 full names
        vis_map = {
            "VS": "VERTEX",
            "HS": "HULL",
            "DS": "DOMAIN",
            "GS": "GEOMETRY",
            "PS": "PIXEL",
            "ALL": "ALL",
            "VERTEX": "VERTEX",
            "HULL": "HULL",
            "DOMAIN": "DOMAIN",
            "GEOMETRY": "GEOMETRY",
            "PIXEL": "PIXEL",
        }
        normalized = []
        for v in vis_list:
            if not isinstance(v, str):
                raise ValueError(
                    f"root_signature parameter {idx} has invalid visibility entry: {v}"
                )
            up = v.upper()
            if up not in vis_map:
                raise ValueError(
                    f"root_signature parameter {idx} has unsupported visibility '{v}'"
                )
            normalized.append(vis_map[up])
        # deduplicate and sort visibility into canonical D3D12 order
        canonical_order = [
            "ALL",
            "VERTEX",
            "HULL",
            "DOMAIN",
            "GEOMETRY",
            "PIXEL",
        ]
        seen = {v: True for v in normalized}
        sorted_vis = [v for v in canonical_order if v in seen]
        param["visibility"] = sorted_vis
        if ptype != "descriptor_table":
            continue
        # normalize param-level domains if present
        param_domains = param.get("domains")
        if isinstance(param_domains, str):
            param_domains = [param_domains]
        elif param_domains is None:
            param_domains = []
        # if any param-level domains declared, ensure they exist
        for dom_name in param_domains:
            if dom_name not in domain_by_id:
                raise ValueError(
                    f"root_signature parameter {idx} references unknown domain '{dom_name}' in 'domains' property"
                )
        ranges = param.get("ranges", [])
        for r_i, r in enumerate(ranges):
            dom = r.get("domain")
            if isinstance(dom, str):
                doms = [dom]
            elif isinstance(dom, list):
                doms = dom
            else:
                doms = []
            # include param-level domains as implicit list (backwards compatibility)
            doms = doms + param_domains
            if not doms:
                raise ValueError(
                    f"root_signature parameter {idx} range {r_i} must specify 'domain'"
                )
            # check each referenced domain exists and basic consistency
            # when multiple domains back a single range, base_shader_register
            # must match the first domain's register token. Only enforce
            # register equality for the first domain.
            first_dom = doms[0] if doms else None
            for dom_entry in doms:
                domain = domain_by_id.get(dom_entry)
                if domain is None:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} references unknown domain '{dom_entry}'"
                    )
                kind = domain.get("kind")
                rt = r.get("range_type")
                # Ensure kinds are normalized to uppercase earlier; compare directly
                if kind is not None and rt is not None and kind != rt:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} range_type '{rt}' does not match domain '{dom_entry}' kind '{kind}'"
                    )
                # check register/space consistency when present on the domain
                dom_reg = domain.get("register")
                dom_space = domain.get("space")
                # dom_reg is token like 't0' or 'b1'; base_shader_register may be token or integer
                if dom_reg is not None:
                    try:
                        reg_num = int(
                            "".join([c for c in str(dom_reg) if c.isdigit()])
                        )
                    except Exception:
                        reg_num = None
                    if reg_num is not None:
                        base = r.get("base_shader_register")
                        if base is not None:
                            # normalize base to integer if it's a token like 't0' or a numeric value
                            if isinstance(base, str):
                                try:
                                    base_num = int(
                                        "".join(
                                            [c for c in base if c.isdigit()]
                                        )
                                    )
                                except Exception:
                                    base_num = None
                            else:
                                try:
                                    base_num = int(base)
                                except Exception:
                                    base_num = None
                            # Only enforce register equality for the first domain when
                            # this range references multiple domains.
                            if dom_entry == first_dom:
                                if (
                                    base_num is not None
                                    and int(base_num) != reg_num
                                ):
                                    raise ValueError(
                                        f"root_signature parameter {idx} range {r_i} base_shader_register {base} does not match domain '{dom_entry}' register {dom_reg}"
                                    )
                if dom_space is not None:
                    space = r.get("register_space")
                    if space is not None:
                        # normalize textual or numeric forms to integers
                        if isinstance(space, str):
                            try:
                                space_num = int(
                                    "".join([c for c in space if c.isdigit()])
                                )
                            except Exception:
                                space_num = None
                        else:
                            try:
                                space_num = int(space)
                            except Exception:
                                space_num = None
                        if isinstance(dom_space, str):
                            try:
                                dom_space_num = int(
                                    "".join(
                                        [c for c in dom_space if c.isdigit()]
                                    )
                                )
                            except Exception:
                                dom_space_num = None
                        else:
                            try:
                                dom_space_num = int(dom_space)
                            except Exception:
                                dom_space_num = None
                        if (
                            space_num is not None
                            and dom_space_num is not None
                            and int(space_num) != int(dom_space_num)
                        ):
                            raise ValueError(
                                f"root_signature parameter {idx} range {r_i} register_space {space} does not match domain '{dom_entry}' space {dom_space}"
                            )

    # Build root signature parameter name set for domain->root_table validation
    # Accept any root parameter name; later enforce that descriptor domains
    # point to descriptor_table parameters when required.
    param_names = {p.get("name"): p for p in root_sig}
    # Validate domain.root_table references
    for d in domains:
        rt = d.get("root_table")
        # For production SSoT enforce that descriptor domains (SRV/UAV/SAMPLER)
        # must explicitly specify the root_table. CBV domains may omit it.
        if d.get("kind") in ("SRV", "UAV", "SAMPLER") and rt is None:
            raise ValueError(
                f"Domain '{d.get('id')}' of kind '{d.get('kind')}' must specify 'root_table' to map it into a descriptor_table"
            )
        if rt is None:
            continue
        if rt not in param_names:
            raise ValueError(
                f"Domain '{d.get('id')}' references unknown root_signature parameter '{rt}'"
            )
        # If this is a descriptor domain, ensure the referenced parameter is a descriptor_table
        if d.get("kind") in ("SRV", "UAV", "SAMPLER"):
            param = param_names.get(rt)
            if param is None or param.get("type") != "descriptor_table":
                raise ValueError(
                    f"Domain '{d.get('id')}' of kind '{d.get('kind')}' must reference a descriptor_table root parameter, but '{rt}' is not a descriptor_table"
                )


def generate(
    input_yaml,
    out_cpp,
    out_hlsl,
    *,
    dry_run: bool = False,
    schema_path: str | None = None,
):
    with open(input_yaml, "r", encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    # Validate input against schema if available
    # Resolve schema using helper: prefer explicit CLI-provided path, then
    # script directory, then repository Bindless folder.
    resolved = find_schema(schema_path, __file__)
    validate_input(doc, resolved)
    # Additional cross-document validations not expressible in JSON Schema
    validate_domain_references(doc)
    defaults = doc.get("defaults", {})
    invalid = defaults.get("invalid_index", 0xFFFFFFFF)
    domains = doc.get("domains", [])
    # Build domain lookup by id for cross-validation with root_signature ranges
    domain_by_id = {d.get("id"): d for d in domains}
    # Validate optional root_signature cross-links
    root_sig = doc.get("root_signature", [])
    for idx, param in enumerate(root_sig):
        ptype = param.get("type")
        # normalize descriptor_table-level domains to list
        param_domains = param.get("domains")
        if isinstance(param_domains, str):
            param_domains = [param_domains]
        elif param_domains is None:
            param_domains = []
        if ptype == "descriptor_table":
            ranges = param.get("ranges", [])
            # if descriptor_table has a top-level domains list, allow shorthand
            if param_domains:
                # validate each domain exists
                for dom_name in param_domains:
                    if dom_name not in domain_by_id:
                        raise ValueError(
                            f"root_signature parameter {idx} references unknown domain '{dom_name}' in 'domains' property"
                        )
            # Explicit-only policy: do not infer missing fields. Presence of
            # required fields is enforced by the JSON schema, but do an
            # additional runtime check to ensure ranges have range_type and
            # num_descriptors and that domains of resource kinds reference a root_table.
            for r_i, r in enumerate(ranges):
                dom = r.get("domain")
                # normalize domain references to list
                if isinstance(dom, str):
                    doms = [dom]
                elif isinstance(dom, list):
                    doms = dom
                else:
                    doms = []
                # include param-level domains as implicit domain list for the parameter
                doms = doms + param_domains
                if not doms:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} must specify 'domain' or parameter-level 'domains' for explicit mapping"
                    )
                # ensure required fields are present explicitly
                if r.get("range_type") is None:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} missing required 'range_type'"
                    )
                if r.get("num_descriptors") is None:
                    raise ValueError(
                        f"root_signature parameter {idx} range {r_i} missing required 'num_descriptors'"
                    )
                for dom_entry in doms:
                    domain = domain_by_id.get(dom_entry)
                    if domain is None:
                        raise ValueError(
                            f"root_signature parameter {idx} range {r_i} references unknown domain '{dom_entry}'"
                        )
                    # basic consistency: kind must match range_type
                    kind = domain.get("kind")
                    rt = r.get("range_type")
                    kind_map = {
                        "srv": "SRV",
                        "cbv": "CBV",
                        "sampler": "SAMPLER",
                        "uav": "UAV",
                    }
                    expected = kind_map.get(kind)
                    if (
                        expected is not None
                        and rt is not None
                        and expected != rt
                    ):
                        raise ValueError(
                            f"root_signature parameter {idx} range {r_i} range_type '{rt}' does not match domain '{dom_entry}' kind '{kind}'"
                        )
                    # if domain contains register/space, check they match base_shader_register/register_space when present
                    dom_reg = domain.get("register")
                    dom_space = domain.get("space")
                    if dom_reg is not None:
                        try:
                            reg_num = int(
                                "".join(
                                    [c for c in str(dom_reg) if c.isdigit()]
                                )
                            )
                        except Exception:
                            reg_num = None
                        if reg_num is not None:
                            base = r.get("base_shader_register")
                            if base is not None:
                                if isinstance(base, str):
                                    try:
                                        base_num = int(
                                            "".join(
                                                [c for c in base if c.isdigit()]
                                            )
                                        )
                                    except Exception:
                                        base_num = None
                                else:
                                    try:
                                        base_num = int(base)
                                    except Exception:
                                        base_num = None
                                # Only enforce register equality for the first domain
                                first_dom = doms[0] if doms else None
                                if dom_entry == first_dom:
                                    if (
                                        base_num is not None
                                        and int(base_num) != reg_num
                                    ):
                                        raise ValueError(
                                            f"root_signature parameter {idx} range {r_i} base_shader_register {base} does not match domain '{dom_entry}' register {dom_reg}"
                                        )
                    if dom_space is not None:
                        space = r.get("register_space")
                        if space is not None:
                            if isinstance(space, str):
                                try:
                                    space_num = int(
                                        "".join(
                                            [c for c in space if c.isdigit()]
                                        )
                                    )
                                except Exception:
                                    space_num = None
                            else:
                                try:
                                    space_num = int(space)
                                except Exception:
                                    space_num = None
                            if isinstance(dom_space, str):
                                try:
                                    dom_space_num = int(
                                        "".join(
                                            [
                                                c
                                                for c in dom_space
                                                if c.isdigit()
                                            ]
                                        )
                                    )
                                except Exception:
                                    dom_space_num = None
                            else:
                                try:
                                    dom_space_num = int(dom_space)
                                except Exception:
                                    dom_space_num = None
                            if (
                                space_num is not None
                                and dom_space_num is not None
                                and int(space_num) != int(dom_space_num)
                            ):
                                raise ValueError(
                                    f"root_signature parameter {idx} range {r_i} register_space {space} does not match domain '{dom_entry}' space {dom_space}"
                                )
    # Build root signature parameter name set for domain->root_table validation
    param_names = {
        p.get("name"): p
        for p in root_sig
        if p.get("type") == "descriptor_table"
    }
    # Validate domain.root_table references
    for d in domains:
        rt = d.get("root_table")
        if rt is None:
            continue
            # Enforce explicit binding: resource domains must specify root_table
            if d.get("kind") in ("srv", "uav", "sampler") and rt is None:
                raise ValueError(
                    f"Domain '{d.get('id')}' of kind '{d.get('kind')}' must specify 'root_table' to map it into a descriptor_table"
                )
            if rt is None:
                continue
            if rt not in param_names:
                raise ValueError(
                    f"Domain '{d.get('id')}' references unknown root_signature descriptor_table '{rt}'"
                )

    # Additional semantic checks: domain address spaces must not overlap
    # when mapped into the same descriptor root_table. For each descriptor
    # root_table, gather domains that reference it and ensure their
    # [domain_base, domain_base+capacity) ranges do not overlap.
    rt_to_domains = {}
    for d in domains:
        rt = d.get("root_table")
        if rt is None:
            continue
        rt_to_domains.setdefault(rt, []).append(d)

    for rt, dlist in rt_to_domains.items():
        # collect ranges and check pairwise overlaps
        intervals = []
        for d in dlist:
            base = d.get("domain_base")
            cap = d.get("capacity")
            if base is None or cap is None:
                # domain_base/capacity are required at schema level, but
                # double-check here for safety
                raise ValueError(
                    f"Domain '{d.get('id')}' in root_table '{rt}' must specify 'domain_base' and 'capacity'"
                )
            start = int(base)
            end = start + int(cap)
            intervals.append((start, end, d.get("id")))
        # sort by start and check adjacent overlaps
        intervals.sort(key=lambda t: t[0])
        for i in range(1, len(intervals)):
            prev = intervals[i - 1]
            cur = intervals[i]
            if cur[0] < prev[1]:
                raise ValueError(
                    f"Domain address ranges overlap in root_table '{rt}': '{prev[2]}' [{prev[0]},{prev[1]}) overlaps '{cur[2]}' [{cur[0]},{cur[1]})"
                )

    # UAV-specific validation: if a domain is kind==UAV and declares
    # 'uav_counter_register', ensure it is a valid token and that any
    # descriptor_table ranges that reference this domain do not set
    # num_descriptors to 'unbounded' (counters imply bounded ranges).
    for d in domains:
        if d.get("kind") == "UAV":
            ucr = d.get("uav_counter_register")
            if ucr is not None:
                if not isinstance(ucr, str) or not ucr.startswith("u"):
                    raise ValueError(
                        f"Domain '{d.get('id')}' has invalid 'uav_counter_register' value '{ucr}'"
                    )
            # check ranges referencing this domain
            for idx, param in enumerate(root_sig):
                if param.get("type") != "descriptor_table":
                    continue
                for r_i, r in enumerate(param.get("ranges", [])):
                    dom = r.get("domain")
                    doms = (
                        [dom]
                        if isinstance(dom, str)
                        else dom if dom is not None else []
                    )
                    doms = doms + (param.get("domains") or [])
                    if d.get("id") in doms:
                        nd = r.get("num_descriptors")
                        if isinstance(nd, str) and nd == "unbounded":
                            raise ValueError(
                                f"Descriptor_table parameter {idx} range {r_i} references UAV domain '{d.get('id')}' which cannot be 'unbounded' when a UAV counter is present"
                            )

    # CBV array validations: if a root parameter is type==cbv and has
    # cbv_array_size, ensure the shader_register/register_space align with
    # any referenced domain (if the cbv domain is declared) and that the
    # cbv array size does not exceed domain capacity when mapped.
    for idx, param in enumerate(root_sig):
        if param.get("type") == "cbv":
            arr_size = param.get("cbv_array_size")
            if arr_size is not None:
                # If this CBV maps to a domain (via domains or name), validate
                # mapping. We treat param.name as potential domain root mapping
                # if domains reference this parameter via root_table.
                name = param.get("name")
                # find domains that reference this parameter as root_table
                mapped = [d for d in domains if d.get("root_table") == name]
                for d in mapped:
                    cap = d.get("capacity")
                    if cap is not None and int(arr_size) > int(cap):
                        raise ValueError(
                            f"CBV parameter '{name}' cbv_array_size {arr_size} exceeds domain '{d.get('id')}' capacity {cap}"
                        )
    ts = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())
    domain_consts = render_cpp_domains(domains)
    content_cpp = TEMPLATE_CPP.format(
        src=input_yaml, ts=ts, invalid=invalid, domain_consts=domain_consts
    )
    domain_defs = render_hlsl_domains(domains)
    content_hlsl = TEMPLATE_HLSL.format(
        src=input_yaml, ts=ts, invalid=invalid, domain_defs=domain_defs
    )

    # Prepare runtime JSON descriptor (machine-friendly)
    # Include heaps/mappings runtime fragment when present and keep the
    # normalized fragment for rendering during dry-run.
    heaps_runtime_fragment = {}
    if doc.get("heaps"):
        heaps_runtime_fragment = heaps_module.validate_heaps_and_mappings(doc)

    runtime_desc = {
        "source": input_yaml,
        "generated": ts,
        "defaults": defaults,
        "domains": domains,
        # include heaps/mappings runtime fragment when present
        **heaps_runtime_fragment,
        "symbols": doc.get("symbols", {}),
        "root_signature": doc.get("root_signature", []),
    }
    out_json = os.path.splitext(out_cpp)[0] + ".json"

    if dry_run:
        print(f"[DRY RUN] Would generate C++ header: {out_cpp}")
        print(f"[DRY RUN] Would generate HLSL header: {out_hlsl}")
        print(f"[DRY RUN] Would generate runtime JSON descriptor: {out_json}")
        # (heaps snippets suppressed in dry-run)
        # (no full runtime JSON dump in dry-run)
        print(f"[DRY RUN] Validation successful, templates processed")
    return True
    # Write runtime JSON descriptor
    json_changed = False
    try:
        js = json.dumps(runtime_desc, indent=2)
        json_changed = atomic_write(out_json, js)
    except Exception as e:
        raise RuntimeError(
            f"Failed to write runtime JSON descriptor {out_json}: {e}"
        ) from e

    changed_cpp = atomic_write(out_cpp, content_cpp)
    changed_hlsl = atomic_write(out_hlsl, content_hlsl)
    return changed_cpp or changed_hlsl
