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
        cpu_cap = int(h.get("cpu_visible_capacity"))
        shader_cap = int(h.get("shader_visible_capacity"))
        base = int(h.get("base_index"))

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

        # For shader-visible heaps the important capacity is shader_visible_capacity;
        # cpu_visible_capacity may be zero when a heap is exclusively shader-visible.
        if shader_visible:
            if shader_cap <= 0:
                raise ValueError(
                    f"Heap '{name}' shader_visible_capacity must be > 0 for shader-visible heaps"
                )
        else:
            if cpu_cap <= 0:
                raise ValueError(
                    f"Heap '{name}' cpu_visible_capacity must be > 0"
                )

        # Compute interval [base, base+cpu_cap)
        start = base
        end = base + cpu_cap
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
        lines.append(
            f"static constexpr uint32_t kHeapBase_{name} = {int(h_dict['base_index'])}u;"
        )
        lines.append(
            f"static constexpr uint32_t kHeapCpuCapacity_{name} = {int(h_dict['cpu_visible_capacity'])}u;"
        )
        lines.append(
            f"static constexpr uint32_t kHeapShaderCapacity_{name} = {int(h_dict['shader_visible_capacity'])}u;"
        )
        lines.append(
            f"static constexpr bool kHeapShaderVisible_{name} = {str(bool(h_dict['shader_visible'])).lower()};"
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
        lines.append(
            f"static const uint {up}_HEAP_BASE = {int(h_dict['base_index'])};"
        )
        lines.append(
            f"static const uint {up}_HEAP_CPU_CAPACITY = {int(h_dict['cpu_visible_capacity'])};"
        )
        lines.append("")
    return "\n".join(lines)
