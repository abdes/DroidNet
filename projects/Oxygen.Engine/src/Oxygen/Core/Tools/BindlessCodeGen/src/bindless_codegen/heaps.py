"""Heap allocation strategy validation and rendering helpers.

This module implements semantic checks for the `heaps` and `mappings`
sections of the BindingSlots SSoT and produces runtime JSON fragments and
small C++/HLSL constant snippets consumed by the main generator.
"""

from pathlib import Path
from typing import Dict, Any, List, Tuple


def _normalize_heaps(raw_heaps: Any) -> Dict[str, Any]:
    """Normalize `heaps` section to a dict keyed by heap id."""
    raw = raw_heaps or {}
    if isinstance(raw, list):
        heaps: Dict[str, Any] = {}
        for h in raw:
            if not isinstance(h, dict):
                continue
            hid = h.get("id")
            if not hid:
                raise ValueError(
                    "Heap entry in 'heaps' array missing required 'id' field"
                )
            heaps[hid] = h
        return heaps
    if isinstance(raw, dict):
        return raw
    raise ValueError("'heaps' must be an array or object")


def _normalize_mappings(raw_mappings: Any) -> Dict[str, Any]:
    """Normalize `mappings` to a dict keyed by domain id."""
    raw = raw_mappings or {}
    if isinstance(raw, list):
        mappings: Dict[str, Any] = {}
        for m in raw:
            if not isinstance(m, dict):
                continue
            domain = m.get("domain")
            if not domain:
                raise ValueError(
                    "Mapping entry missing required 'domain' field"
                )
            mappings[domain] = m
        return mappings
    if isinstance(raw, dict):
        return raw
    raise ValueError("'mappings' must be an array or object")


def _validate_heap_entries(heaps: Dict[str, Any]) -> List[Tuple[int, int, str]]:
    """Validate individual heap entries and return their global index ranges."""
    ranges: List[Tuple[int, int, str]] = []
    for name, h in heaps.items():
        htype = h.get("type")
        shader_visible = h.get("shader_visible")
        capacity = int(h.get("capacity", 0))
        base = int(h.get("base_index"))

        # Validate heap id consistency if it encodes type:visibility
        if isinstance(name, str) and ":" in name:
            try:
                id_type, id_vis = name.split(":", 1)
            except ValueError:
                id_type, id_vis = name, ""
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

        # Type/visibility constraints
        if htype in ("RTV", "DSV") and shader_visible:
            raise ValueError(
                f"Heap '{name}' type '{htype}' cannot be shader_visible"
            )
        if shader_visible and htype not in ("CBV_SRV_UAV", "SAMPLER"):
            raise ValueError(
                f"Heap '{name}' is shader_visible but type '{htype}' does not allow shader visibility"
            )

        # Capacity must be positive
        if capacity <= 0:
            raise ValueError(f"Heap '{name}' capacity must be > 0")

        ranges.append((base, base + capacity, name))
    return ranges


def _check_global_heap_overlaps(ranges: List[Tuple[int, int, str]]) -> None:
    """Ensure no two heaps overlap in global index space."""
    ranges.sort(key=lambda t: t[0])
    for i in range(1, len(ranges)):
        prev = ranges[i - 1]
        cur = ranges[i]
        if cur[0] < prev[1]:
            raise ValueError(
                f"Heap address ranges overlap: '{prev[2]}' [{prev[0]},{prev[1]}) overlaps '{cur[2]}' [{cur[0]},{cur[1]})"
            )


def _validate_mapping_heap_names(
    mappings: Dict[str, Any], heaps: Dict[str, Any]
) -> None:
    """Validate that every mapping references an existing heap."""
    for rv, m in mappings.items():
        heap_name = m.get("heap")
        if heap_name not in heaps:
            raise ValueError(
                f"Mapping for '{rv}' references unknown heap '{heap_name}'"
            )


def _validate_per_heap_domain_capacity(
    doc: Dict[str, Any],
    heaps: Dict[str, Any],
    mappings: Dict[str, Any],
) -> None:
    """Ensure domains mapped into a heap fit and do not exceed heap capacity.

    Checks each heap's local ranges for containment, total capacity, and overlaps.
    """
    domains_list = doc.get("domains", []) or []
    domains_by_id = {
        d.get("id"): d for d in domains_list if isinstance(d, dict)
    }

    # Group mappings by heap id
    mappings_by_heap: Dict[str, List[Tuple[str, Dict[str, Any]]]] = {}
    for dom_id, m in mappings.items():
        heap_name = m.get("heap")
        mappings_by_heap.setdefault(heap_name, []).append((dom_id, m))

    for heap_name, entries in mappings_by_heap.items():
        h = heaps.get(heap_name)
        if not h:
            raise ValueError(f"Mapping references unknown heap '{heap_name}'")
        heap_cap = int(h.get("capacity", 0))
        if heap_cap <= 0:
            raise ValueError(f"Heap '{heap_name}' capacity must be > 0")

        total_caps = 0
        intervals_local: List[Tuple[int, int, str]] = []
        for dom_id, m in entries:
            d = domains_by_id.get(dom_id)
            if not d:
                raise ValueError(
                    f"Mapping for domain '{dom_id}' has no corresponding domain entry"
                )
            dom_cap = d.get("capacity")
            if dom_cap is None:
                raise ValueError(
                    f"Domain '{dom_id}' used in heap mapping must define 'capacity'"
                )
            dom_cap_i = int(dom_cap)
            if dom_cap_i < 0:
                raise ValueError(
                    f"Domain '{dom_id}' capacity must be non-negative"
                )
            local_base = int(m.get("local_base", 0))
            if local_base < 0:
                raise ValueError(
                    f"Mapping for domain '{dom_id}' has negative local_base {local_base}"
                )
            local_end = local_base + dom_cap_i
            if local_end > heap_cap:
                raise ValueError(
                    f"Mapping for domain '{dom_id}' exceeds heap '{heap_name}' capacity: "
                    f"[{local_base},{local_end}) with capacity {dom_cap_i} > heap capacity {heap_cap}"
                )
            total_caps += dom_cap_i
            intervals_local.append((local_base, local_end, dom_id))

        if total_caps > heap_cap:
            raise ValueError(
                f"Sum of capacities for domains mapped to heap '{heap_name}' ({total_caps}) exceeds heap capacity ({heap_cap})"
            )

        intervals_local.sort(key=lambda t: t[0])
        for i in range(1, len(intervals_local)):
            prev = intervals_local[i - 1]
            cur = intervals_local[i]
            if cur[0] < prev[1]:
                raise ValueError(
                    f"Heap-local mapping overlap in heap '{heap_name}': "
                    f"domain '{prev[2]}' [{prev[0]},{prev[1]}) overlaps domain '{cur[2]}' [{cur[0]},{cur[1]})"
                )


def validate_heaps_and_mappings(doc: Dict[str, Any]) -> Dict[str, Any]:
    """Validate `heaps` and `mappings` and return runtime fragment.

    Keeps this function small by delegating to helpers for normalization and
    validations to maintain low cyclomatic complexity.
    """
    heaps = _normalize_heaps(doc.get("heaps"))
    mappings = _normalize_mappings(doc.get("mappings"))

    ranges = _validate_heap_entries(heaps)
    _check_global_heap_overlaps(ranges)
    _validate_mapping_heap_names(mappings, heaps)
    _validate_per_heap_domain_capacity(doc, heaps, mappings)

    return {"heaps": heaps, "mappings": mappings}


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
