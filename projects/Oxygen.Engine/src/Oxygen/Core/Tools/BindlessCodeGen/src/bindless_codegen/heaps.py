"""Heap allocation strategy validation and rendering helpers.

This module implements semantic checks for the `heaps` and `mappings`
sections of the BindingSlots SSoT and produces runtime JSON fragments and
small C++/HLSL constant snippets consumed by the main generator.
"""

from pathlib import Path
from typing import Dict, Any, List, Tuple


def validate_heaps_and_mappings(doc: Dict[str, Any]) -> Dict[str, Any]:
    """Validate `heaps` and `mappings` sections and return a runtime
    descriptor fragment to be merged into the main runtime JSON.

    Raises ValueError on semantic problems.
    """
    raw_heaps = doc.get("heaps", {}) or {}
    raw_mappings = doc.get("mappings", {}) or {}

    # Normalize heaps: support either an array of heap objects or a mapping
    heaps: Dict[str, Any] = {}
    if isinstance(raw_heaps, list):
        for h in raw_heaps:
            hid = h.get("id")
            if not hid:
                raise ValueError(
                    "Heap entry in 'heaps' array missing required 'id' field"
                )
            heaps[hid] = h
    elif isinstance(raw_heaps, dict):
        heaps = raw_heaps
    else:
        raise ValueError("'heaps' must be an array or object")

    # Normalize mappings: support either array of mapping objects or a mapping
    mappings: Dict[str, Any] = {}
    if isinstance(raw_mappings, list):
        for m in raw_mappings:
            domain = m.get("domain")
            if not domain:
                raise ValueError(
                    "Mapping entry missing required 'domain' field"
                )
            mappings[domain] = m
    elif isinstance(raw_mappings, dict):
        mappings = raw_mappings
    else:
        raise ValueError("'mappings' must be an array or object")

    # Ensure heap names are unique (they are keys, so implicit) and collect ranges
    ranges: List[Tuple[int, int, str]] = []
    for name, h in heaps.items():
        # required fields assumed present by JSON Schema; validate cross-field rules
        htype = h.get("type")
        shader_visible = h.get("shader_visible")
        capacity = int(h.get("capacity", 0))
        base = int(h.get("base_index"))

        # If heap id looks like "TYPE:vis", validate it matches type and visibility
        if isinstance(name, str) and ":" in name:
            try:
                id_type, id_vis = name.split(":", 1)
            except ValueError:
                id_type, id_vis = name, ""
            # Normalize
            id_type = id_type.strip()
            id_vis = id_vis.strip().lower()
            expected_vis = "gpu" if bool(shader_visible) else "cpu"
            if id_type != str(htype):
                raise ValueError(
                    f"Heap id '{name}' type prefix '{id_type}' does not match declared type '{htype}'"
                )
            if id_vis not in ("gpu", "cpu"):
                raise ValueError(
                    f"Heap id '{name}' visibility suffix '{id_vis}' must be 'gpu' or 'cpu'"
                )
            if id_vis != expected_vis:
                raise ValueError(
                    f"Heap id '{name}' visibility suffix '{id_vis}' conflicts with shader_visible={bool(shader_visible)}"
                )

        # RTV/DSV must be CPU-only
        if htype in ("RTV", "DSV") and shader_visible:
            raise ValueError(
                f"Heap '{name}' type '{htype}' cannot be shader_visible"
            )

        # shader_visible only allowed for CBV_SRV_UAV and SAMPLER
        if shader_visible and htype not in ("CBV_SRV_UAV", "SAMPLER"):
            raise ValueError(
                f"Heap '{name}' is shader_visible but type '{htype}' does not allow shader visibility"
            )

        # Capacity must be positive regardless of visibility
        eff_cap = capacity
        if eff_cap <= 0:
            raise ValueError(f"Heap '{name}' capacity must be > 0")

        # Compute global index interval [base, base + eff_cap)
        start = base
        end = base + eff_cap
        ranges.append((start, end, name))

    # Check for range overlaps anywhere in the global index space
    ranges.sort(key=lambda t: t[0])
    for i in range(1, len(ranges)):
        prev = ranges[i - 1]
        cur = ranges[i]
        if cur[0] < prev[1]:
            raise ValueError(
                f"Heap address ranges overlap: '{prev[2]}' [{prev[0]},{prev[1]}) overlaps '{cur[2]}' [{cur[0]},{cur[1]})"
            )

    # Validate mappings reference existing heaps. Mappings no longer include an
    # explicit 'visibility' token; the mapping simply ties a domain to a heap id.
    for rv, m in mappings.items():
        heap_name = m.get("heap")
        if heap_name not in heaps:
            raise ValueError(
                f"Mapping for '{rv}' references unknown heap '{heap_name}'"
            )

    # Compose runtime fragment
    runtime = {"heaps": heaps, "mappings": mappings}
    return runtime


def render_cpp_heaps(heaps: Dict[str, Any]) -> str:
    """Render C++ snippets for heaps.

    Accept either a dict keyed by heap id or a list of heap objects. Normalize
    to a dict internally for clarity.
    """
    # Normalize if a list was passed
    if isinstance(heaps, list):
        _heaps = {}
        for h in heaps:
            if not isinstance(h, dict):
                continue
            hid = h.get("id")
            if not hid:
                continue
            _heaps[hid] = h
    else:
        _heaps = heaps

    lines: List[str] = []
    for name, h in _heaps.items():
        # ensure the analyzer understands h is a mapping
        h_dict: Dict[str, Any] = h  # type: ignore
        dbg = h_dict.get("debug_name")
        if dbg:
            lines.append(f"// {dbg}")
        # C++ constants: kUpperCamelCase
        lines.append(
            f"static constexpr uint32_t k{name}HeapBase = {int(h_dict['base_index'])}u;"
        )
        lines.append(
            f"static constexpr uint32_t k{name}HeapCapacity = {int(h_dict['capacity'])}u;"
        )
        lines.append(
            f"static constexpr bool k{name}HeapShaderVisible = {str(bool(h_dict['shader_visible'])).lower()};"
        )
        lines.append("")
    return "\n".join(lines)


def render_hlsl_heaps(heaps: Dict[str, Any]) -> str:
    """Render HLSL snippets for heaps.

    Accept either a dict keyed by heap id or a list of heap objects. Normalize
    to a dict internally for clarity.
    """
    if isinstance(heaps, list):
        _heaps = {}
        for h in heaps:
            if not isinstance(h, dict):
                continue
            hid = h.get("id")
            if not hid:
                continue
            _heaps[hid] = h
    else:
        _heaps = heaps

    lines: List[str] = []
    for name, h in _heaps.items():
        up = name.upper()
        h_dict: Dict[str, Any] = h  # type: ignore
        dbg = h_dict.get("debug_name")
        if dbg:
            lines.append(f"// {dbg}")
        # HLSL constants: K_UPPER_SNAKE_CASE
        lines.append(
            f"static const uint K_{up}_HEAP_BASE = {int(h_dict['base_index'])};"
        )
        lines.append(
            f"static const uint K_{up}_HEAP_CAPACITY = {int(h_dict['capacity'])};"
        )
        lines.append("")
    return "\n".join(lines)


def _build_heap_key(htype: str, shader_visible: bool) -> str:
    """Mimic D3D12HeapAllocationStrategy::BuildHeapKey format.

    Keys look like: "CBV_SRV_UAV:gpu" or "SAMPLER:cpu".
    """
    vis = "gpu" if shader_visible else "cpu"
    return f"{htype}:{vis}"


def build_d3d12_strategy_json(heaps: Dict[str, Any]) -> Dict[str, Any]:
    """Compose a D3D12 heap strategy JSON fragment wrapped under top-level 'heaps'.

    Input `heaps` may be a dict keyed by heap id or a list of heap objects.
    Output structure is: { "heaps": { key -> { capacity, base_index, policy... } } }
    where key looks like "CBV_SRV_UAV:gpu" or "SAMPLER:cpu".
    """
    # Normalize
    if isinstance(heaps, list):
        _heaps = {
            h.get("id"): h for h in heaps if isinstance(h, dict) and h.get("id")
        }
    else:
        _heaps = heaps or {}

    flat: Dict[str, Any] = {}
    for _, h in _heaps.items():
        htype = str(h.get("type"))
        shader_vis = bool(h.get("shader_visible"))
        key = _build_heap_key(htype, shader_vis)
        cap = int(h.get("capacity", 0))
        entry = {
            "capacity": cap,
            "shader_visible": shader_vis,
            "allow_growth": bool(h.get("allow_growth", False)),
            # Conservative defaults; engine currently sets fixed, non-growing heaps
            "growth_factor": float(h.get("growth_factor", 0.0)),
            "max_growth_iterations": int(h.get("max_growth_iterations", 0)),
            "base_index": int(h.get("base_index", 0)),
        }
        dbg = h.get("debug_name")
        if dbg:
            entry["debug_name"] = dbg
        flat[key] = entry
    return {"heaps": flat}
