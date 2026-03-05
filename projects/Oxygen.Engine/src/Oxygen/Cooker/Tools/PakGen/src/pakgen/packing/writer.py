"""Binary writer orchestrating emission of a PAK given a BuildPlan + PakPlan.

Phase 6 refactor: the writer no longer performs its own layout math for the
major sections (resource regions, tables, asset descriptors block, directory,
footer). Instead it *consumes* the immutable :class:`PakPlan` produced by the
planner. Any divergence between emitted byte positions and the plan raises an
error (defensive invariant) to guarantee the plan is the single source of
truth for offsets.

Residual intra-region details (per-blob alignment inside a resource region,
variable geometry payload emission) remain locally computed but are validated
against the size/offset expectations captured in the plan.
"""

from __future__ import annotations
from pathlib import Path
import struct
import zlib

from ..logging import get_logger, section
from ..reporting import get_reporter
from .planner import BuildPlan, PakPlan
from .browse_index import (
    BrowseIndexEntrySpec,
    build_browse_index_payload,
    derive_virtual_path_from_asset_name,
)
from .constants import (
    DATA_ALIGNMENT,
    TABLE_ALIGNMENT,
    FOOTER_SIZE,
    ASSET_HEADER_SIZE,
    SCRIPT_SLOT_RECORD_SIZE,
)
from .packers import (
    pack_header,
    pack_footer,
    pack_buffer_resource_descriptor,
    pack_texture_resource_descriptor,
    pack_audio_resource_descriptor,
    pack_script_resource_descriptor,
    pack_script_asset_descriptor,
    pack_input_action_asset_descriptor,
    pack_input_mapping_context_asset_descriptor_and_payload,
    pack_script_slot_record,
    pack_directory_entry,
    pack_material_asset_descriptor,
    pack_shader_reference_entries,
    pack_geometry_asset_descriptor,
    pack_scene_asset_descriptor_and_payload,
    pack_mesh_descriptor,
    pack_submesh_descriptor,
    pack_mesh_view_descriptor,
    pack_name_string,
    pack_physics_resource_descriptor,
    pack_physics_material_asset_descriptor,
    pack_collision_shape_asset_descriptor,
    pack_physics_scene_asset_descriptor_and_payload,
    pack_rigid_body_binding_record,
    pack_collider_binding_record,
    pack_character_binding_record,
    pack_soft_body_binding_record,
    pack_joint_binding_record,
    pack_vehicle_binding_record,
    pack_aggregate_binding_record,
    resolve_procedural_params_blob,
)

__all__ = ["write_pak"]

RESOURCE_TYPES = ["texture", "buffer", "audio", "script", "physics"]


def _pad_to(f, target_offset: int):
    """Write zero padding until file position reaches ``target_offset``."""
    pos = f.tell()
    if pos > target_offset:
        raise RuntimeError(
            f"Writer position {pos} surpassed planned offset {target_offset}"
        )
    if pos < target_offset:
        f.write(b"\x00" * (target_offset - pos))


def _write_resource_regions_from_plan(f, build: BuildPlan, plan: PakPlan):
    logger = get_logger()
    # Map region name -> RegionPlan for quick lookup (skip empty size regions)
    region_map = {r.name: r for r in plan.regions if r.size > 0}
    data_offsets: dict[str, list[int]] = {r: [] for r in RESOURCE_TYPES}
    rep = get_reporter()
    for rtype in RESOURCE_TYPES:
        region_plan = region_map.get(rtype)
        blobs = build.resources.data_blobs.get(rtype, [])
        if not region_plan or region_plan.size == 0:
            # Expect no blobs and size==0
            if blobs:
                raise RuntimeError(
                    f"Plan has empty region for {rtype} but blobs present"
                )
            continue
        rep.start_task(
            f"write.region.{rtype}", f"{rtype.title()} region", total=len(blobs)
        )
        _pad_to(f, region_plan.offset)
        region_start = f.tell()
        inner_cursor = 0
        for blob in blobs:
            # Align each blob to DATA_ALIGNMENT like planner sizing logic.
            pad = (
                DATA_ALIGNMENT - (inner_cursor % DATA_ALIGNMENT)
            ) % DATA_ALIGNMENT
            if pad:
                f.write(b"\x00" * pad)
                inner_cursor += pad
            data_offsets[rtype].append(region_start + inner_cursor)
            f.write(blob)
            inner_cursor += len(blob)
            rep.advance(f"write.region.{rtype}")
        written = f.tell() - region_start
        if written != region_plan.size:
            raise RuntimeError(
                f"Region size mismatch for {rtype}: plan={region_plan.size} written={written}"
            )
        rep.end_task(
            f"write.region.{rtype}",
            blobs=len(blobs),
            bytes=written,
            planned=region_plan.size,
        )
    return data_offsets


def _write_resource_tables_from_plan(
    f, build: BuildPlan, pak_plan: PakPlan, data_offsets: dict[str, list[int]]
):
    logger = get_logger()
    table_map = {t.name: t for t in pak_plan.tables if t.count > 0}
    table_info: dict[str, tuple[int, int, int]] = {}
    rep = get_reporter()
    for rtype in RESOURCE_TYPES:
        descs = build.resources.desc_fields.get(rtype, [])
        blobs = build.resources.data_blobs.get(rtype, [])
        tplan = table_map.get(rtype)
        if not tplan:
            if descs:
                raise RuntimeError(
                    f"Plan missing table for {rtype} with descriptors present"
                )
            table_info[rtype] = (0, 0, 0)
            continue
        rep.start_task(
            f"write.table.{rtype}", f"{rtype.title()} table", total=len(descs)
        )
        _pad_to(f, tplan.offset)
        if len(descs) != tplan.count:
            raise RuntimeError(
                f"Descriptor count mismatch for {rtype}: plan={tplan.count} actual={len(descs)}"
            )
        for i, spec in enumerate(descs):
            data_off = (
                data_offsets[rtype][i] if i < len(data_offsets[rtype]) else 0
            )
            size = len(blobs[i]) if i < len(blobs) else 0
            # Force all-zero descriptor for the buffer sentinel
            if rtype == "buffer" and spec.get("name") == "__sentinel_buffer":
                data_off = 0
                size = 0
            if rtype == "script" and spec.get("name") == "__sentinel_script":
                data_off = 0
                size = 0
            if rtype == "buffer":
                desc = pack_buffer_resource_descriptor(spec, data_off, size)
            elif rtype == "texture":
                desc = pack_texture_resource_descriptor(spec, data_off, size)
            elif rtype == "audio":
                desc = pack_audio_resource_descriptor(spec, data_off, size)
            elif rtype == "script":
                desc = pack_script_resource_descriptor(spec, data_off, size)
            elif rtype == "physics":
                desc = pack_physics_resource_descriptor(spec, data_off, size)
            else:  # pragma: no cover
                raise RuntimeError(f"Unknown resource type {rtype}")
            f.write(desc)
            rep.advance(f"write.table.{rtype}")
        written = f.tell() - tplan.offset
        expected = tplan.count * tplan.entry_size
        if written != expected:
            raise RuntimeError(
                f"Table size mismatch for {rtype}: plan expects {expected} wrote {written}"
            )
        table_info[rtype] = (tplan.offset, tplan.count, tplan.entry_size)
        rep.end_task(
            f"write.table.{rtype}",
            entries=tplan.count,
            planned=tplan.count,
        )
    return table_info


def _patch_crc(path: Path):
    data = path.read_bytes()
    # Exclude CRC field (last 12 bytes: 4 CRC + 8 footer magic) in calculation
    crc_field_offset = len(data) - 12
    crc = (
        zlib.crc32(data[:crc_field_offset] + data[crc_field_offset + 4 :])
        & 0xFFFFFFFF
    )
    with path.open("r+b") as f:
        f.seek(-12, 2)
        f.write(struct.pack("<I", crc))
    return crc


def _prepare_scene_script_slots(
    build: BuildPlan,
    pak_plan: PakPlan,
    header_builder,
    geometry_name_to_key: dict[str, bytes],
    script_name_to_key: dict[str, bytes],
):
    material_count = len(build.assets.material_assets)
    geometry_count = len(build.assets.geometry_assets)
    script_count = len(build.assets.script_assets)
    input_action_count = len(build.assets.input_action_assets)
    input_mapping_context_count = len(build.assets.input_mapping_context_assets)
    scenes = build.assets.scene_assets
    cache: dict[int, tuple[bytes, bytes, list[dict[str, int | bytes]]]] = {}
    all_slots: list[bytes] = []
    global_slot_base = 0
    for idx, asset_plan in enumerate(pak_plan.assets):
        if asset_plan.asset_type != "scene":
            continue
        s_idx = (
            idx
            - material_count
            - geometry_count
            - script_count
            - input_action_count
            - input_mapping_context_count
        )
        scene_spec, _scene_key, _atype, _align = scenes[s_idx]
        if not isinstance(scene_spec, dict):
            scene_spec = {}
        scene_spec = dict(scene_spec)
        scene_spec.setdefault("type", "scene")
        base_desc, payload, slot_infos = pack_scene_asset_descriptor_and_payload(
            scene_spec,
            header_builder=header_builder,
            geometry_name_to_key=geometry_name_to_key,
            script_name_to_key=script_name_to_key,
            scripting_slot_base_index=global_slot_base,
        )
        cache[idx] = (base_desc, payload, slot_infos)
        for slot_info in slot_infos:
            params_rel = int(slot_info.get("params_relative_offset", 0) or 0)
            params_abs = (
                int(asset_plan.descriptor_offset) + params_rel if params_rel > 0 else 0
            )
            all_slots.append(
                pack_script_slot_record(
                    script_asset_key=bytes(slot_info["script_asset_key"]),
                    params_array_offset=params_abs,
                    params_count=int(slot_info.get("params_count", 0) or 0),
                    execution_order=int(slot_info.get("execution_order", 0) or 0),
                    flags=int(slot_info.get("flags", 0) or 0),
                )
            )
        global_slot_base += len(slot_infos)
    return cache, all_slots


def _write_script_slot_table_from_plan(
    f,
    pak_plan: PakPlan,
    slot_records: list[bytes],
):
    table_map = {t.name: t for t in pak_plan.tables if t.count > 0}
    tplan = table_map.get("script_slot")
    if not tplan:
        if slot_records:
            raise RuntimeError("Plan missing script_slot table but slots were generated")
        return (0, 0, 0)
    if tplan.entry_size != SCRIPT_SLOT_RECORD_SIZE:
        raise RuntimeError(
            f"Script slot table entry size mismatch: plan={tplan.entry_size} expected={SCRIPT_SLOT_RECORD_SIZE}"
        )
    _pad_to(f, tplan.offset)
    if len(slot_records) != tplan.count:
        raise RuntimeError(
            f"Script slot count mismatch: plan={tplan.count} actual={len(slot_records)}"
        )
    for rec in slot_records:
        if len(rec) != tplan.entry_size:
            raise RuntimeError(
                f"Script slot entry size mismatch: plan={tplan.entry_size} actual={len(rec)}"
            )
        f.write(rec)
    return (tplan.offset, tplan.count, tplan.entry_size)


def _write_assets_and_directory_from_plan(
    f,
    build: BuildPlan,
    pak_plan: PakPlan,
    scene_cache: dict[int, tuple[bytes, bytes, list[dict[str, int | bytes]]]],
):
    """Emit asset descriptors (materials, geometries, scenes) and directory per plan.

    Uses plan.assets ordering & descriptor offsets. Geometry variable blobs are
    emitted immediately after their base descriptor (matching planner sizing).
    Returns (directory_offset, directory_size, asset_count).
    """
    logger = get_logger()
    from .constants import ASSET_TYPE_MAP  # local import

    # Builders mirror legacy writer; kept local for clarity.
    def header_builder(asset_dict):
        name = asset_dict.get("name", "")
        type_name = asset_dict.get("type")
        asset_type = ASSET_TYPE_MAP.get(type_name, 0)
        version = asset_dict.get("version", 1)
        streaming_priority = asset_dict.get("streaming_priority", 0)
        content_hash = asset_dict.get("content_hash", 0)
        variant_flags = asset_dict.get("variant_flags", 0)
        name_bytes = pack_name_string(name, 64)
        header = (
            struct.pack("<B", asset_type)
            + name_bytes
            + struct.pack("<B", version)
            + struct.pack("<B", streaming_priority)
            + struct.pack("<Q", content_hash)
            + struct.pack("<I", variant_flags)
        )
        if len(header) != ASSET_HEADER_SIZE:  # pragma: no cover - defensive
            raise RuntimeError("Asset header size mismatch")
        return header

    # Retained parameter for legacy API; shader reference records are now
    # always packed explicitly after the fixed descriptor using
    # pack_shader_reference_entries(), so this builder is a no-op.
    def shader_refs_builder(_shader_refs):  # noqa: D401 - simple
        return b""

    # Simple material assets list for submesh packing (name/key tuples)
    simple_material_assets = []
    for m in build.assets.material_assets:
        if not isinstance(m, dict):
            continue
        spec = m.get("spec", {}) if isinstance(m.get("spec"), dict) else {}
        name = spec.get("name")
        key = m.get("asset_key", b"\x00" * 16)
        if isinstance(name, str) and isinstance(key, (bytes, bytearray)):
            simple_material_assets.append({"name": name, "key": key})

    def lods_builder(geom):
        out = b""
        lods = geom.get("lods", []) or []
        for lod in lods:
            if not isinstance(lod, dict):
                continue
            mesh_type = int(lod.get("mesh_type", 0) or 0)
            procedural_blob = b""
            if mesh_type == 2:
                procedural_blob = resolve_procedural_params_blob(
                    lod, build.base_dir
                )
            out += pack_mesh_descriptor(
                lod,
                build.resources.index_map,
                pack_name_string,
                procedural_params_size_override=(
                    len(procedural_blob) if mesh_type == 2 else None
                ),
            )
            if procedural_blob:
                out += procedural_blob
            submeshes = lod.get("submeshes", []) or []
            for sub in submeshes:
                if not isinstance(sub, dict):
                    continue
                out += pack_submesh_descriptor(
                    sub, simple_material_assets, pack_name_string
                )
                for mv in sub.get("mesh_views", []) or []:
                    if not isinstance(mv, dict):
                        continue
                    out += pack_mesh_view_descriptor(mv)
        return out

    # Material / geometry / scene sources from build plan (original specs)
    materials = build.assets.material_assets
    geometries = build.assets.geometry_assets
    scripts = build.assets.script_assets
    input_actions = build.assets.input_action_assets
    input_mapping_contexts = build.assets.input_mapping_context_assets
    scenes = build.assets.scene_assets
    material_count = len(materials)
    geometry_count = len(geometries)
    script_count = len(scripts)
    input_action_count = len(input_actions)
    input_mapping_context_count = len(input_mapping_contexts)
    scene_count = len(scenes)
    physics_material_count = len(build.assets.physics_material_assets)
    collision_shape_count = len(build.assets.collision_shape_assets)
    physics_scene_count = len(build.assets.physics_scene_assets)

    geometry_name_to_key: dict[str, bytes] = {}
    for geom_spec, asset_key, _atype, _align in geometries:
        if isinstance(geom_spec, dict):
            nm = geom_spec.get("name")
            if isinstance(nm, str) and isinstance(
                asset_key, (bytes, bytearray)
            ):
                geometry_name_to_key[nm] = bytes(asset_key)

    input_action_name_to_key: dict[str, bytes] = {}
    for input_action_spec, asset_key, _atype, _align in input_actions:
        if isinstance(input_action_spec, dict):
            nm = input_action_spec.get("name")
            if isinstance(nm, str) and isinstance(asset_key, (bytes, bytearray)):
                input_action_name_to_key[nm] = bytes(asset_key)

    physics_material_name_to_asset_key: dict[str, bytes] = {}
    collision_shape_name_to_asset_key: dict[str, bytes] = {}
    physics_resource_name_to_index: dict[str, int] = {}

    physics_descs = build.resources.desc_fields.get("physics", [])
    for resource_index, resource_spec in enumerate(physics_descs):
        if not isinstance(resource_spec, dict):
            continue
        resource_name = resource_spec.get("name")
        if isinstance(resource_name, str):
            physics_resource_name_to_index[resource_name] = resource_index

    for pm_spec in build.assets.physics_material_assets:
        if not isinstance(pm_spec, dict):
            continue
        raw_spec = pm_spec.get("spec")
        if not isinstance(raw_spec, dict):
            continue
        pm_name = raw_spec.get("name")
        pm_key = pm_spec.get("asset_key")
        if isinstance(pm_name, str) and isinstance(pm_key, (bytes, bytearray)):
            physics_material_name_to_asset_key[pm_name] = bytes(pm_key)
    for cs_entry in build.assets.collision_shape_assets:
        cs_spec = cs_entry[0]
        cs_key = cs_entry[1]
        if not isinstance(cs_spec, dict):
            continue
        cs_name = cs_spec.get("name")
        if isinstance(cs_name, str) and isinstance(cs_key, (bytes, bytearray)):
            collision_shape_name_to_asset_key[cs_name] = bytes(cs_key)

    # Emit descriptors following plan order
    rep = get_reporter()
    rep.start_task(
        "write.assets", "Asset descriptors", total=len(pak_plan.assets)
    )
    for idx, asset_plan in enumerate(pak_plan.assets):
        _pad_to(f, asset_plan.descriptor_offset)
        if asset_plan.asset_type == "material":
            mat = materials[idx]  # materials come first in plan
            raw_spec = mat.get("spec") if isinstance(mat, dict) else None
            if not isinstance(raw_spec, dict):
                raw_spec = mat if isinstance(mat, dict) else {}
            raw_spec.setdefault("type", "material")
            try:
                desc = pack_material_asset_descriptor(
                    raw_spec,
                    build.resources.index_map,
                    header_builder=header_builder,
                    shader_refs_builder=shader_refs_builder,
                )
            except Exception as exc:  # pragma: no cover
                logger.error("Skipping material: %s", exc)
                continue
            f.write(desc)
            # Emit shader reference entries (variable extra) based on plan size
            shader_refs = raw_spec.get("shader_references", []) or []
            if shader_refs and asset_plan.variable_extra_size:
                shader_stages = int(raw_spec.get("shader_stages", 0))
                shader_blob = pack_shader_reference_entries(
                    shader_stages, shader_refs
                )
                if len(shader_blob) != asset_plan.variable_extra_size:
                    raise RuntimeError(
                        "Shader refs size mismatch plan=%d actual=%d"
                        % (
                            asset_plan.variable_extra_size,
                            len(shader_blob),
                        )
                    )
                f.write(shader_blob)
            if len(desc) != asset_plan.descriptor_size:
                raise RuntimeError(
                    f"Material size mismatch plan={asset_plan.descriptor_size} actual={len(desc)}"
                )
        elif asset_plan.asset_type == "geometry":
            # Geometry index in build list: (idx - material_count)
            g_idx = idx - material_count
            geom_spec, asset_key, _atype, alignment = geometries[g_idx]
            geom_spec.setdefault("type", "geometry")
            base_desc = pack_geometry_asset_descriptor(
                geom_spec, header_builder=header_builder
            )
            f.write(base_desc)
            var_blob = lods_builder(geom_spec)
            if var_blob:
                f.write(var_blob)
            expected_total = (
                asset_plan.descriptor_size + asset_plan.variable_extra_size
            )
            written = len(base_desc) + len(var_blob)
            if written != expected_total:
                raise RuntimeError(
                    f"Geometry size mismatch plan_total={expected_total} actual={written}"
                )
        elif asset_plan.asset_type == "script":
            s_idx = idx - material_count - geometry_count
            script_spec, _script_key, _atype, _align = scripts[s_idx]
            if not isinstance(script_spec, dict):
                script_spec = {}
            script_spec = dict(script_spec)
            script_spec.setdefault("type", "script")
            base_desc = pack_script_asset_descriptor(
                script_spec,
                build.resources.index_map,
                header_builder=header_builder,
            )
            f.write(base_desc)
            expected_total = asset_plan.descriptor_size
            if len(base_desc) != expected_total:
                raise RuntimeError(
                    f"Script size mismatch plan_total={expected_total} actual={len(base_desc)}"
                )
        elif asset_plan.asset_type == "input_action":
            ia_idx = idx - material_count - geometry_count - script_count
            input_action_spec, _input_action_key, _atype, _align = input_actions[
                ia_idx
            ]
            if not isinstance(input_action_spec, dict):
                input_action_spec = {}
            input_action_spec = dict(input_action_spec)
            input_action_spec.setdefault("type", "input_action")
            base_desc = pack_input_action_asset_descriptor(
                input_action_spec,
                header_builder=header_builder,
            )
            f.write(base_desc)
            expected_total = asset_plan.descriptor_size
            if len(base_desc) != expected_total:
                raise RuntimeError(
                    f"InputAction size mismatch plan_total={expected_total} actual={len(base_desc)}"
                )
        elif asset_plan.asset_type == "input_mapping_context":
            imc_idx = (
                idx - material_count - geometry_count - script_count - input_action_count
            )
            imc_spec, _imc_key, _atype, _align = input_mapping_contexts[imc_idx]
            if not isinstance(imc_spec, dict):
                imc_spec = {}
            imc_spec = dict(imc_spec)
            imc_spec.setdefault("type", "input_mapping_context")
            base_desc, payload = pack_input_mapping_context_asset_descriptor_and_payload(
                imc_spec,
                header_builder=header_builder,
                action_name_to_key=input_action_name_to_key,
            )
            f.write(base_desc)
            if payload:
                f.write(payload)
            expected_total = (
                asset_plan.descriptor_size + asset_plan.variable_extra_size
            )
            written = len(base_desc) + len(payload)
            if written != expected_total:
                raise RuntimeError(
                    f"InputMappingContext size mismatch plan_total={expected_total} actual={written}"
                )
        elif asset_plan.asset_type == "scene":
            s_idx = (
                idx
                - material_count
                - geometry_count
                - script_count
                - input_action_count
                - input_mapping_context_count
            )
            scene_spec, _scene_key, _atype, _align = scenes[s_idx]
            if isinstance(scene_spec, dict) and scene_spec.get("type") == "physics_scene":
                raise RuntimeError(
                    "Physics sidecar asset cannot be emitted with scene packer"
                )
            cached = scene_cache.get(idx)
            if not cached:
                raise RuntimeError(f"Missing cached scene packing payload for asset index {idx}")
            base_desc, payload, _slot_infos = cached
            f.write(base_desc)
            if payload:
                f.write(payload)
            expected_total = (
                asset_plan.descriptor_size + asset_plan.variable_extra_size
            )
            written = len(base_desc) + len(payload)
            if written != expected_total:
                raise RuntimeError(
                    f"Scene size mismatch plan_total={expected_total} actual={written}"
                )
        elif asset_plan.asset_type == "physics_material":
            pm_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count
            spec = build.assets.physics_material_assets[pm_idx]
            raw_spec = spec.get("spec") if isinstance(spec, dict) else None
            if not isinstance(raw_spec, dict):
                raw_spec = spec if isinstance(spec, dict) else {}
            desc = pack_physics_material_asset_descriptor(raw_spec, header_builder=header_builder)
            f.write(desc)
            if len(desc) != asset_plan.descriptor_size:
                raise RuntimeError(f"PhysicsMaterial size mismatch: plan={asset_plan.descriptor_size} actual={len(desc)}")
        elif asset_plan.asset_type == "collision_shape":
            cs_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count - physics_material_count
            cs_spec, key, _atype, _align = build.assets.collision_shape_assets[cs_idx]
            # collision shapes may need a physics resource index if they are mesh-based
            res_idx = build.resources.index_map.get("physics", {}).get(cs_spec.get("resource_name", ""), 0)
            desc = pack_collision_shape_asset_descriptor(
                cs_spec,
                resource_index=res_idx,
                header_builder=header_builder,
                physics_material_name_to_asset_key=physics_material_name_to_asset_key,
            )
            f.write(desc)
            if len(desc) != asset_plan.descriptor_size:
                raise RuntimeError(f"CollisionShape size mismatch: plan={asset_plan.descriptor_size} actual={len(desc)}")
        elif asset_plan.asset_type == "physics_scene":
            ps_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count - physics_material_count - collision_shape_count
            ps_spec, key, _atype, _align = build.assets.physics_scene_assets[ps_idx]
            if not isinstance(ps_spec, dict):
                ps_spec = {}
            ps_spec = dict(ps_spec)
            ps_spec.setdefault("type", "physics_scene")
            desc, payload = pack_physics_scene_asset_descriptor_and_payload(
                ps_spec,
                header_builder=header_builder,
                shape_name_to_asset_key=collision_shape_name_to_asset_key,
                physics_material_name_to_asset_key=physics_material_name_to_asset_key,
                physics_resource_name_to_index=physics_resource_name_to_index,
            )
            f.write(desc)
            if payload:
                f.write(payload)
            expected_total = (
                asset_plan.descriptor_size + asset_plan.variable_extra_size
            )
            written = len(desc) + len(payload)
            if written != expected_total:
                raise RuntimeError(
                    f"PhysicsScene size mismatch: plan_total={expected_total} actual={written}"
                )
            if len(desc) != asset_plan.descriptor_size:
                raise RuntimeError(f"PhysicsScene size mismatch: plan={asset_plan.descriptor_size} actual={len(desc)}")
        else:  # pragma: no cover
            raise RuntimeError(f"Unknown asset type {asset_plan.asset_type}")
        rep.advance("write.assets")
    rep.end_task("write.assets")

    # Directory
    if not pak_plan.assets:
        return 0, 0, 0
    directory_plan = pak_plan.directory
    _pad_to(f, directory_plan.offset)
    # Build directory entries from plan assets in order
    # Need actual keys: fetch from build plan lists
    entry_written = 0
    rep.start_task(
        "write.directory", "Asset directory", total=len(pak_plan.assets)
    )
    for idx, asset_plan in enumerate(pak_plan.assets):
        entry_pos = f.tell()
        if asset_plan.asset_type == "material":
            mat = materials[idx]
            key = (
                mat.get("asset_key", b"\x00" * 16)
                if isinstance(mat, dict)
                else b"\x00" * 16
            )
        elif asset_plan.asset_type == "geometry":
            g_idx = idx - material_count
            _geom_spec, key, _atype, _align = geometries[g_idx]
        elif asset_plan.asset_type == "script":
            s_idx = idx - material_count - geometry_count
            _script_spec, key, _atype, _align = scripts[s_idx]
        elif asset_plan.asset_type == "input_action":
            ia_idx = idx - material_count - geometry_count - script_count
            _input_action_spec, key, _atype, _align = input_actions[ia_idx]
        elif asset_plan.asset_type == "input_mapping_context":
            imc_idx = (
                idx - material_count - geometry_count - script_count - input_action_count
            )
            _imc_spec, key, _atype, _align = input_mapping_contexts[imc_idx]
        elif asset_plan.asset_type == "scene":
            s_idx = (
                idx
                - material_count
                - geometry_count
                - script_count
                - input_action_count
                - input_mapping_context_count
            )
            _scene_spec, key, _atype, _align = scenes[s_idx]
        elif asset_plan.asset_type == "physics_material":
            pm_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count
            spec = build.assets.physics_material_assets[pm_idx]
            key = spec.get("asset_key", b"\x00" * 16)
        elif asset_plan.asset_type == "collision_shape":
            cs_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count - physics_material_count
            _cs_spec, key, _atype, _align = build.assets.collision_shape_assets[cs_idx]
        elif asset_plan.asset_type == "physics_scene":
            ps_idx = idx - material_count - geometry_count - script_count - input_action_count - input_mapping_context_count - scene_count - physics_material_count - collision_shape_count
            _ps_spec, key, _atype, _align = build.assets.physics_scene_assets[ps_idx]
        else:  # pragma: no cover
            raise RuntimeError(f"Unknown asset type {asset_plan.asset_type}")
        asset_type_code = ASSET_TYPE_MAP.get(asset_plan.asset_type, 0)
        total_desc_size = int(
            asset_plan.descriptor_size + (asset_plan.variable_extra_size or 0)
        )
        entry = pack_directory_entry(
            asset_key=key,
            asset_type=asset_type_code,
            entry_offset=entry_pos,
            desc_offset=asset_plan.descriptor_offset,
            desc_size=total_desc_size,
        )
        f.write(entry)
        entry_written += 1
        rep.advance("write.directory")
    written_size = f.tell() - directory_plan.offset
    if written_size != directory_plan.size:
        raise RuntimeError(
            f"Directory size mismatch plan={directory_plan.size} actual={written_size}"
        )
    if entry_written != directory_plan.asset_count:
        raise RuntimeError(
            f"Directory asset count mismatch plan={directory_plan.asset_count} actual={entry_written}"
        )
    logger.info(
        "Asset directory: %d entries size=%d (planned %d)",
        entry_written,
        written_size,
        directory_plan.size,
    )
    rep.end_task("write.directory")
    return (
        directory_plan.offset,
        directory_plan.size,
        directory_plan.asset_count,
    )


def write_pak(
    build_plan: BuildPlan, pak_plan: PakPlan, output_path: Path
) -> int:
    """Write a PAK file strictly following ``pak_plan``.

    All major section offsets & sizes are validated against the plan to ensure
    the plan is authoritative. Returns total bytes written.
    """
    logger = get_logger()
    version = pak_plan.version
    content_version = pak_plan.content_version
    guid = pak_plan.guid
    with section(f"Write PAK {output_path.name}"):
        with output_path.open("wb") as f:
            # Header (always at 0)
            f.write(pack_header(version, content_version, guid))
            # Resource regions
            data_offsets = _write_resource_regions_from_plan(
                f, build_plan, pak_plan
            )
            # Resource tables
            table_info = _write_resource_tables_from_plan(
                f, build_plan, pak_plan, data_offsets
            )

            from .constants import ASSET_TYPE_MAP  # local import

            def header_builder(asset_dict):
                name = asset_dict.get("name", "")
                type_name = asset_dict.get("type")
                asset_type = ASSET_TYPE_MAP.get(type_name, 0)
                version = asset_dict.get("version", 1)
                streaming_priority = asset_dict.get("streaming_priority", 0)
                content_hash = asset_dict.get("content_hash", 0)
                variant_flags = asset_dict.get("variant_flags", 0)
                name_bytes = pack_name_string(name, 64)
                header = (
                    struct.pack("<B", asset_type)
                    + name_bytes
                    + struct.pack("<B", version)
                    + struct.pack("<B", streaming_priority)
                    + struct.pack("<Q", content_hash)
                    + struct.pack("<I", variant_flags)
                )
                if len(header) != ASSET_HEADER_SIZE:
                    raise RuntimeError("Asset header size mismatch")
                return header

            geometry_name_to_key: dict[str, bytes] = {}
            for geom_spec, asset_key, _atype, _align in build_plan.assets.geometry_assets:
                if not isinstance(geom_spec, dict):
                    continue
                name = geom_spec.get("name")
                if isinstance(name, str) and isinstance(asset_key, (bytes, bytearray)):
                    geometry_name_to_key[name] = bytes(asset_key)

            script_name_to_key: dict[str, bytes] = {}
            for script_spec, asset_key, _atype, _align in build_plan.assets.script_assets:
                if not isinstance(script_spec, dict):
                    continue
                name = script_spec.get("name")
                if isinstance(name, str) and isinstance(asset_key, (bytes, bytearray)):
                    script_name_to_key[name] = bytes(asset_key)

            scene_cache, slot_records = _prepare_scene_script_slots(
                build_plan,
                pak_plan,
                header_builder=header_builder,
                geometry_name_to_key=geometry_name_to_key,
                script_name_to_key=script_name_to_key,
            )
            table_info["script_slot"] = _write_script_slot_table_from_plan(
                f, pak_plan, slot_records
            )
            # Assets + directory
            directory_offset, directory_size, asset_count = (
                _write_assets_and_directory_from_plan(
                    f, build_plan, pak_plan, scene_cache
                )
            )

            # Embedded browse index (after directory, before footer)
            browse_index_offset = 0
            browse_index_size = 0
            bix_plan = pak_plan.browse_index
            if bix_plan and bix_plan.size > 0:
                _pad_to(f, bix_plan.offset)
                payload, entry_count, string_table_size = (
                    build_browse_index_payload(
                        [
                            BrowseIndexEntrySpec(
                                asset_key=bytes.fromhex(a.key_hex),
                                virtual_path=derive_virtual_path_from_asset_name(
                                    a.name
                                ),
                            )
                            for a in pak_plan.assets
                        ]
                    )
                )
                if entry_count != bix_plan.entry_count:
                    raise RuntimeError(
                        "Browse index entry_count mismatch: "
                        f"plan={bix_plan.entry_count} actual={entry_count}"
                    )
                if string_table_size != bix_plan.string_table_size:
                    raise RuntimeError(
                        "Browse index string_table_size mismatch: "
                        f"plan={bix_plan.string_table_size} actual={string_table_size}"
                    )
                if len(payload) != bix_plan.size:
                    raise RuntimeError(
                        "Browse index size mismatch: "
                        f"plan={bix_plan.size} actual={len(payload)}"
                    )
                f.write(payload)
                browse_index_offset = bix_plan.offset
                browse_index_size = bix_plan.size

            # Footer
            footer_plan = pak_plan.footer
            _pad_to(f, footer_plan.offset)
            # Build region/table lookup for footer packer
            region_lookup = {
                r.name: (r.offset, r.size) for r in pak_plan.regions
            }
            table_lookup = {
                t.name: (t.offset, t.count, t.entry_size)
                for t in pak_plan.tables
            }
            f.write(
                pack_footer(
                    directory_offset=directory_offset,
                    directory_size=directory_size,
                    asset_count=asset_count,
                    texture_region=region_lookup.get("texture", (0, 0)),
                    buffer_region=region_lookup.get("buffer", (0, 0)),
                    audio_region=region_lookup.get("audio", (0, 0)),
                    script_region=region_lookup.get("script", (0, 0)),
                    physics_region=region_lookup.get("physics", (0, 0)),
                    texture_table=table_lookup.get("texture", (0, 0, 0)),
                    buffer_table=table_lookup.get("buffer", (0, 0, 0)),
                    audio_table=table_lookup.get("audio", (0, 0, 0)),
                    script_resource_table=table_lookup.get("script", (0, 0, 0)),
                    script_slot_table=table_lookup.get("script_slot", (0, 0, 0)),
                    physics_resource_table=table_lookup.get("physics", (0, 0, 0)),
                    browse_index_offset=browse_index_offset,
                    browse_index_size=browse_index_size,
                    pak_crc32=0,
                )
            )
        crc = _patch_crc(output_path)
        size = output_path.stat().st_size
    if size != pak_plan.file_size:
        raise RuntimeError(
            f"File size mismatch vs plan: plan={pak_plan.file_size} actual={size}"
        )
    logger.info(
        "Wrote PAK size=%d bytes crc=0x%08x assets=%d (plan size %d)",
        size,
        crc,
        asset_count,
        pak_plan.file_size,
    )
    return size
