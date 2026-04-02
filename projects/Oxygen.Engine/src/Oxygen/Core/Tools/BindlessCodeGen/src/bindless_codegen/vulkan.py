"""Vulkan backend strategy validation helpers."""

from __future__ import annotations

from typing import Any, Dict, List, Tuple


_DESCRIPTOR_TYPE_TO_ACCESS_CLASSES: dict[str, set[str]] = {
    "SAMPLED_IMAGE": {"texture_srv"},
    "STORAGE_IMAGE": {"texture_uav"},
    "UNIFORM_TEXEL_BUFFER": {"buffer_srv"},
    "STORAGE_TEXEL_BUFFER": {"buffer_srv", "buffer_uav"},
    "UNIFORM_BUFFER": {"constant_buffer"},
    "STORAGE_BUFFER": {"buffer_srv", "buffer_uav"},
    "SAMPLER": {"sampler"},
    "ACCELERATION_STRUCTURE_KHR": {"ray_tracing_accel_structure_srv"},
}


def _normalize_descriptor_sets(
    raw_sets: Any,
) -> Dict[str, Dict[str, Any]]:
    descriptor_sets: Dict[str, Dict[str, Any]] = {}
    for descriptor_set in raw_sets or []:
        if not isinstance(descriptor_set, dict):
            continue
        set_id = descriptor_set.get("id")
        if not set_id:
            raise ValueError("Vulkan descriptor_set entry missing required 'id'")
        if set_id in descriptor_sets:
            raise ValueError(f"Duplicate Vulkan descriptor_set id '{set_id}'")
        descriptor_sets[str(set_id)] = descriptor_set
    return descriptor_sets


def _normalize_bindings(raw_bindings: Any) -> Dict[str, Dict[str, Any]]:
    bindings: Dict[str, Dict[str, Any]] = {}
    for binding in raw_bindings or []:
        if not isinstance(binding, dict):
            continue
        binding_id = binding.get("id")
        if not binding_id:
            raise ValueError("Vulkan binding entry missing required 'id'")
        if binding_id in bindings:
            raise ValueError(f"Duplicate Vulkan binding id '{binding_id}'")
        bindings[str(binding_id)] = binding
    return bindings


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
                "Vulkan domain_realization missing required 'domain'"
            )
        if domain in realizations:
            raise ValueError(
                f"Duplicate Vulkan domain realization for '{domain}'"
            )
        realizations[str(domain)] = realization
    return realizations


def _validate_bindings(
    descriptor_sets: Dict[str, Dict[str, Any]],
    bindings: Dict[str, Dict[str, Any]],
) -> None:
    set_indices: set[int] = set()
    for set_id, descriptor_set in descriptor_sets.items():
        set_index = int(descriptor_set.get("set", -1))
        if set_index in set_indices:
            raise ValueError(
                f"Duplicate Vulkan descriptor set index {set_index}"
            )
        set_indices.add(set_index)

    used_slots: set[Tuple[str, int]] = set()
    for binding_id, binding in bindings.items():
        set_id = str(binding.get("set"))
        if set_id not in descriptor_sets:
            raise ValueError(
                f"Vulkan binding '{binding_id}' references unknown "
                f"descriptor_set '{set_id}'"
            )
        slot = (set_id, int(binding.get("binding", -1)))
        if slot in used_slots:
            raise ValueError(
                f"Duplicate Vulkan binding slot set='{set_id}' "
                f"binding={slot[1]}"
            )
        used_slots.add(slot)


def _validate_domain_realizations(
    domains_by_id: Dict[str, Dict[str, Any]],
    bindings: Dict[str, Dict[str, Any]],
    realizations: Dict[str, Dict[str, Any]],
) -> None:
    missing = sorted(set(domains_by_id) - set(realizations))
    if missing:
        raise ValueError(
            f"Vulkan strategy is missing domain realizations for {missing}"
        )

    intervals_by_binding: Dict[str, List[Tuple[int, int, str]]] = {}
    for domain_id, realization in realizations.items():
        domain = domains_by_id.get(domain_id)
        if domain is None:
            raise ValueError(
                f"Vulkan domain realization references unknown ABI domain "
                f"'{domain_id}'"
            )

        binding_id = str(realization.get("binding"))
        binding = bindings.get(binding_id)
        if binding is None:
            raise ValueError(
                f"Vulkan domain realization for '{domain_id}' references "
                f"unknown binding '{binding_id}'"
            )

        descriptor_type = str(binding.get("descriptor_type"))
        access_class = str(domain.get("shader_access_class"))
        allowed_access_classes = _DESCRIPTOR_TYPE_TO_ACCESS_CLASSES[
            descriptor_type
        ]
        if access_class not in allowed_access_classes:
            raise ValueError(
                f"Vulkan domain realization for '{domain_id}' uses binding "
                f"'{binding_id}' descriptor_type '{descriptor_type}', which is "
                f"not compatible with shader_access_class '{access_class}'"
            )

        array_element_base = int(realization.get("array_element_base", 0))
        capacity = int(domain.get("capacity", 0))
        array_element_end = array_element_base + capacity

        descriptor_count = binding.get("descriptor_count")
        if descriptor_count is not None and array_element_end > int(
            descriptor_count
        ):
            raise ValueError(
                f"Vulkan domain realization for '{domain_id}' exceeds binding "
                f"'{binding_id}' descriptor_count: "
                f"[{array_element_base},{array_element_end})"
            )

        intervals_by_binding.setdefault(binding_id, []).append(
            (array_element_base, array_element_end, domain_id)
        )

    for binding_id, intervals in intervals_by_binding.items():
        ordered = sorted(intervals, key=lambda item: item[0])
        for idx in range(1, len(ordered)):
            prev = ordered[idx - 1]
            cur = ordered[idx]
            if cur[0] < prev[1]:
                raise ValueError(
                    f"Vulkan binding-local realization overlap in binding "
                    f"'{binding_id}': domain '{prev[2]}' [{prev[0]},{prev[1]}) "
                    f"overlaps domain '{cur[2]}' [{cur[0]},{cur[1]})"
                )


def _validate_pipeline_layout(
    descriptor_sets: Dict[str, Dict[str, Any]],
    pipeline_layout: List[Dict[str, Any]],
) -> None:
    ids_seen: set[str] = set()
    for entry in pipeline_layout:
        entry_id = str(entry.get("id"))
        if entry_id in ids_seen:
            raise ValueError(
                f"Duplicate Vulkan pipeline_layout id '{entry_id}'"
            )
        ids_seen.add(entry_id)

        if entry.get("type") == "descriptor_set":
            set_ref = str(entry.get("set_ref"))
            if set_ref not in descriptor_sets:
                raise ValueError(
                    f"Vulkan pipeline_layout entry '{entry_id}' references "
                    f"unknown descriptor_set '{set_ref}'"
                )


def validate_vulkan_backend(
    backend_doc: Dict[str, Any],
    domains_by_id: Dict[str, Dict[str, Any]],
) -> Dict[str, Any]:
    strategy = backend_doc.get("strategy") or {}
    descriptor_sets = _normalize_descriptor_sets(
        strategy.get("descriptor_sets")
    )
    bindings = _normalize_bindings(strategy.get("bindings"))
    realizations = _normalize_domain_realizations(
        strategy.get("domain_realizations")
    )

    _validate_bindings(descriptor_sets, bindings)
    _validate_domain_realizations(domains_by_id, bindings, realizations)
    _validate_pipeline_layout(
        descriptor_sets, list(backend_doc.get("pipeline_layout") or [])
    )

    return {
        "descriptor_sets": descriptor_sets,
        "bindings": bindings,
        "domain_realizations": realizations,
    }
