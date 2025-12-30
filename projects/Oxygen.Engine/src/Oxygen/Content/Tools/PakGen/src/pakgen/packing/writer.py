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
)
from .packers import (
    pack_header,
    pack_footer,
    pack_buffer_resource_descriptor,
    pack_texture_resource_descriptor,
    pack_audio_resource_descriptor,
    pack_directory_entry,
    pack_material_asset_descriptor,
    pack_shader_reference_entries,
    pack_geometry_asset_descriptor,
    pack_scene_asset_descriptor_and_payload,
    pack_mesh_descriptor,
    pack_submesh_descriptor,
    pack_mesh_view_descriptor,
    pack_name_string,
)

__all__ = ["write_pak"]

RESOURCE_TYPES = ["texture", "buffer", "audio"]


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
            if rtype == "buffer":
                desc = pack_buffer_resource_descriptor(spec, data_off, size)
            elif rtype == "texture":
                desc = pack_texture_resource_descriptor(spec, data_off, size)
            elif rtype == "audio":
                desc = pack_audio_resource_descriptor(spec, data_off, size)
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


def _write_assets_and_directory_from_plan(
    f, build: BuildPlan, pak_plan: PakPlan
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
            + b"\x00" * 16
        )
        if len(header) != 95:  # pragma: no cover - defensive
            raise RuntimeError("Asset header size mismatch")
        return header

    # Retained parameter for legacy API; shader reference records are now
    # always packed explicitly after the fixed 256-byte descriptor using
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
            out += pack_mesh_descriptor(
                lod, build.resources.index_map, pack_name_string
            )
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
    scenes = build.assets.scene_assets
    material_count = len(materials)
    geometry_count = len(geometries)

    geometry_name_to_key: dict[str, bytes] = {}
    for geom_spec, asset_key, _atype, _align in geometries:
        if isinstance(geom_spec, dict):
            nm = geom_spec.get("name")
            if isinstance(nm, str) and isinstance(
                asset_key, (bytes, bytearray)
            ):
                geometry_name_to_key[nm] = bytes(asset_key)

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
                shader_blob = pack_shader_reference_entries(shader_refs)
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
        elif asset_plan.asset_type == "scene":
            s_idx = idx - material_count - geometry_count
            scene_spec, _scene_key, _atype, _align = scenes[s_idx]
            if not isinstance(scene_spec, dict):
                scene_spec = {}
            scene_spec.setdefault("type", "scene")
            base_desc, payload = pack_scene_asset_descriptor_and_payload(
                scene_spec,
                header_builder=header_builder,
                geometry_name_to_key=geometry_name_to_key,
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
                    f"Scene size mismatch plan_total={expected_total} actual={written}"
                )
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
        elif asset_plan.asset_type == "scene":
            s_idx = idx - material_count - geometry_count
            _scene_spec, key, _atype, _align = scenes[s_idx]
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
    with section(f"Write PAK {output_path.name}"):
        with output_path.open("wb") as f:
            # Header (always at 0)
            f.write(pack_header(version, content_version))
            # Resource regions
            data_offsets = _write_resource_regions_from_plan(
                f, build_plan, pak_plan
            )
            # Resource tables
            table_info = _write_resource_tables_from_plan(
                f, build_plan, pak_plan, data_offsets
            )
            # Assets + directory
            directory_offset, directory_size, asset_count = (
                _write_assets_and_directory_from_plan(f, build_plan, pak_plan)
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
                    texture_table=table_lookup.get("texture", (0, 0, 0)),
                    buffer_table=table_lookup.get("buffer", (0, 0, 0)),
                    audio_table=table_lookup.get("audio", (0, 0, 0)),
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
