"""D3D12 backend strategy validation and rendering helpers."""

from __future__ import annotations

from typing import Any, Dict, List, Tuple


_TABLE_KIND_TO_ACCESS_CLASSES: dict[str, set[str]] = {
    "SRV": {"buffer_srv", "texture_srv", "ray_tracing_accel_structure_srv"},
    "UAV": {"buffer_uav", "texture_uav"},
    "CBV": {"constant_buffer"},
    "SAMPLER": {"sampler"},
}


def _normalize_heaps(raw_heaps: Any) -> Dict[str, Dict[str, Any]]:
    heaps: Dict[str, Dict[str, Any]] = {}
    for heap in raw_heaps or []:
        if not isinstance(heap, dict):
            continue
        heap_id = heap.get("id")
        if not heap_id:
            raise ValueError(
                "D3D12 heap entry missing required 'id' field"
            )
        if heap_id in heaps:
            raise ValueError(f"Duplicate D3D12 heap id '{heap_id}'")
        heaps[str(heap_id)] = heap
    return heaps


def _normalize_tables(raw_tables: Any) -> Dict[str, Dict[str, Any]]:
    tables: Dict[str, Dict[str, Any]] = {}
    for table in raw_tables or []:
        if not isinstance(table, dict):
            continue
        table_id = table.get("id")
        if not table_id:
            raise ValueError(
                "D3D12 table entry missing required 'id' field"
            )
        if table_id in tables:
            raise ValueError(f"Duplicate D3D12 table id '{table_id}'")
        tables[str(table_id)] = table
    return tables


def _normalize_domain_realizations(
    raw_realizations: Any,
) -> Dict[str, Dict[str, Any]]:
    realizations: Dict[str, Dict[str, Any]] = {}
    for realization in raw_realizations or []:
        if not isinstance(realization, dict):
            continue
        domain = realization.get("domain")
        if not domain:
            raise ValueError(
                "D3D12 domain_realization missing required 'domain' field"
            )
        if domain in realizations:
            raise ValueError(
                f"Duplicate D3D12 domain realization for '{domain}'"
            )
        realizations[str(domain)] = realization
    return realizations


def _validate_heap_entries(
    heaps: Dict[str, Dict[str, Any]],
) -> List[Tuple[int, int, str]]:
    ranges: List[Tuple[int, int, str]] = []
    for name, heap in heaps.items():
        heap_type = str(heap.get("type"))
        shader_visible = bool(heap.get("shader_visible"))
        capacity = int(heap.get("capacity", 0))
        base = int(heap.get("base_index", 0))

        if ":" in name:
            heap_type_from_id, heap_vis = name.split(":", 1)
            expected_vis = "gpu" if shader_visible else "cpu"
            if heap_vis not in ("gpu", "cpu"):
                raise ValueError(
                    f"D3D12 heap '{name}' visibility suffix must be 'gpu' or "
                    "'cpu'"
                )
            if heap_type_from_id != heap_type:
                raise ValueError(
                    f"D3D12 heap '{name}' type prefix '{heap_type_from_id}' "
                    f"does not match heap type '{heap_type}'"
                )
            if heap_vis != expected_vis:
                raise ValueError(
                    f"D3D12 heap '{name}' visibility suffix '{heap_vis}' "
                    f"conflicts with shader_visible={shader_visible}"
                )

        if heap_type in ("RTV", "DSV") and shader_visible:
            raise ValueError(
                f"D3D12 heap '{name}' type '{heap_type}' cannot be "
                "shader_visible"
            )
        if shader_visible and heap_type not in ("CBV_SRV_UAV", "SAMPLER"):
            raise ValueError(
                f"D3D12 heap '{name}' is shader_visible but type '{heap_type}' "
                "does not permit shader visibility"
            )
        if capacity <= 0:
            raise ValueError(f"D3D12 heap '{name}' capacity must be > 0")

        ranges.append((base, base + capacity, name))
    return ranges


def _check_global_heap_overlaps(ranges: List[Tuple[int, int, str]]) -> None:
    ordered = sorted(ranges, key=lambda item: item[0])
    for idx in range(1, len(ordered)):
        prev = ordered[idx - 1]
        cur = ordered[idx]
        if cur[0] < prev[1]:
            raise ValueError(
                f"D3D12 heap address ranges overlap: '{prev[2]}' "
                f"[{prev[0]},{prev[1]}) overlaps '{cur[2]}' "
                f"[{cur[0]},{cur[1]})"
            )


def _validate_tables(
    heaps: Dict[str, Dict[str, Any]], tables: Dict[str, Dict[str, Any]]
) -> None:
    for table_id, table in tables.items():
        heap_id = str(table.get("heap"))
        if heap_id not in heaps:
            raise ValueError(
                f"D3D12 table '{table_id}' references unknown heap '{heap_id}'"
            )

        heap = heaps[heap_id]
        descriptor_kind = str(table.get("descriptor_kind"))
        if bool(table.get("unbounded")) and table.get("descriptor_count") is not None:
            raise ValueError(
                f"D3D12 table '{table_id}' cannot specify both 'unbounded' and "
                "'descriptor_count'"
            )

        if not bool(heap.get("shader_visible")):
            raise ValueError(
                f"D3D12 table '{table_id}' must reference a shader-visible heap"
            )

        heap_type = str(heap.get("type"))
        if descriptor_kind == "SAMPLER":
            if heap_type != "SAMPLER":
                raise ValueError(
                    f"D3D12 table '{table_id}' kind SAMPLER must use a "
                    "SAMPLER heap"
                )
        else:
            if heap_type != "CBV_SRV_UAV":
                raise ValueError(
                    f"D3D12 table '{table_id}' kind '{descriptor_kind}' must "
                    "use a CBV_SRV_UAV heap"
                )


def _validate_domain_realizations(
    domains_by_id: Dict[str, Dict[str, Any]],
    heaps: Dict[str, Dict[str, Any]],
    tables: Dict[str, Dict[str, Any]],
    realizations: Dict[str, Dict[str, Any]],
) -> None:
    missing = sorted(set(domains_by_id) - set(realizations))
    if missing:
        raise ValueError(
            f"D3D12 strategy is missing domain realizations for {missing}"
        )

    intervals_by_heap: Dict[str, List[Tuple[int, int, str]]] = {}

    for domain_id, realization in realizations.items():
        domain = domains_by_id.get(domain_id)
        if domain is None:
            raise ValueError(
                f"D3D12 domain realization references unknown ABI domain "
                f"'{domain_id}'"
            )

        table_id = str(realization.get("table"))
        table = tables.get(table_id)
        if table is None:
            raise ValueError(
                f"D3D12 domain realization for '{domain_id}' references "
                f"unknown table '{table_id}'"
            )

        descriptor_kind = str(table.get("descriptor_kind"))
        access_class = str(domain.get("shader_access_class"))
        allowed_access_classes = _TABLE_KIND_TO_ACCESS_CLASSES[descriptor_kind]
        if access_class not in allowed_access_classes:
            raise ValueError(
                f"D3D12 domain realization for '{domain_id}' uses table "
                f"'{table_id}' kind '{descriptor_kind}', which is not "
                f"compatible with shader_access_class '{access_class}'"
            )

        heap_id = str(table.get("heap"))
        heap = heaps[heap_id]
        local_base = int(realization.get("heap_local_base", 0))
        capacity = int(domain.get("capacity", 0))
        local_end = local_base + capacity

        if local_end > int(heap.get("capacity", 0)):
            raise ValueError(
                f"D3D12 domain realization for '{domain_id}' exceeds heap "
                f"'{heap_id}' capacity: [{local_base},{local_end})"
            )

        descriptor_count = table.get("descriptor_count")
        if descriptor_count is not None and local_end > int(descriptor_count):
            raise ValueError(
                f"D3D12 domain realization for '{domain_id}' exceeds table "
                f"'{table_id}' descriptor_count: [{local_base},{local_end})"
            )

        intervals_by_heap.setdefault(heap_id, []).append(
            (local_base, local_end, domain_id)
        )

    for heap_id, intervals in intervals_by_heap.items():
        ordered = sorted(intervals, key=lambda item: item[0])
        for idx in range(1, len(ordered)):
            prev = ordered[idx - 1]
            cur = ordered[idx]
            if cur[0] < prev[1]:
                raise ValueError(
                    f"D3D12 heap-local realization overlap in heap '{heap_id}': "
                    f"domain '{prev[2]}' [{prev[0]},{prev[1]}) overlaps "
                    f"domain '{cur[2]}' [{cur[0]},{cur[1]})"
                )


def validate_d3d12_backend(
    backend_doc: Dict[str, Any],
    domains_by_id: Dict[str, Dict[str, Any]],
) -> Dict[str, Any]:
    strategy = backend_doc.get("strategy") or {}
    heaps = _normalize_heaps(strategy.get("heaps"))
    tables = _normalize_tables(strategy.get("tables"))
    realizations = _normalize_domain_realizations(
        strategy.get("domain_realizations")
    )

    ranges = _validate_heap_entries(heaps)
    _check_global_heap_overlaps(ranges)
    _validate_tables(heaps, tables)
    _validate_domain_realizations(domains_by_id, heaps, tables, realizations)

    return {
        "heaps": heaps,
        "tables": tables,
        "domain_realizations": realizations,
    }


def _build_heap_key(heap_type: str, shader_visible: bool) -> str:
    visibility = "gpu" if shader_visible else "cpu"
    return f"{heap_type}:{visibility}"


def build_d3d12_strategy_json(heaps: Dict[str, Any]) -> Dict[str, Any]:
    """Build the runtime D3D12 heap strategy JSON fragment."""
    flat: Dict[str, Any] = {}
    for heap in (heaps or {}).values():
        heap_type = str(heap.get("type"))
        shader_vis = bool(heap.get("shader_visible"))
        key = _build_heap_key(heap_type, shader_vis)
        entry = {
            "capacity": int(heap.get("capacity", 0)),
            "shader_visible": shader_vis,
            "allow_growth": bool(heap.get("allow_growth", False)),
            "growth_factor": float(heap.get("growth_factor", 0.0)),
            "max_growth_iterations": int(
                heap.get("max_growth_iterations", 0)
            ),
            "base_index": int(heap.get("base_index", 0)),
        }
        debug_name = heap.get("debug_name")
        if debug_name:
            entry["debug_name"] = debug_name
        flat[key] = entry
    return {"heaps": flat}
