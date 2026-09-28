"""Current material and slot wire records, independent of renderer behavior."""

import struct
from uuid import UUID

import pytest

from pakgen.packing.constants import ASSET_HEADER_SIZE
from pakgen.packing.errors import PakError
from pakgen.packing.packers import (
    _pack_material_override_record,
    _pack_renderable_record,
    pack_material_asset_descriptor,
    pack_submesh_descriptor,
)


SLOT_ID = "00000000-0000-0000-0000-000000000001"


def test_submesh_carries_opaque_slot_between_material_and_view_count():
    record = pack_submesh_descriptor(
        {"name": "Surface", "slot_id": SLOT_ID, "material": "Mat", "mesh_views": []},
        [{"name": "Mat", "key": bytes([7]) * 16}],
        lambda name, size: name.encode().ljust(size, b"\0"),
    )
    assert len(record) == 124
    assert record[64:80] == bytes([7]) * 16
    assert record[80:96] == UUID(SLOT_ID).bytes
    assert struct.unpack_from("<I", record, 96) == (0,)


def test_material_override_carries_slot_material_and_full_layout_revision():
    record = _pack_material_override_record(
        {"node_index": 2, "slot_id": SLOT_ID, "material": "Mat", "layout_revision": "ab" * 32},
        {"Mat": bytes([7]) * 16},
        node_count=3,
    )
    assert len(record) == 68
    assert struct.unpack_from("<I", record) == (2,)
    assert record[4:20] == UUID(SLOT_ID).bytes
    assert record[20:36] == bytes([7]) * 16
    assert record[36:] == bytes.fromhex("ab" * 32)


@pytest.mark.parametrize("slot_id", [None, "", "00000000-0000-0000-0000-000000000000", "1"])
def test_material_override_rejects_missing_or_invalid_slot(slot_id):
    with pytest.raises(PakError, match="slot_id"):
        _pack_material_override_record(
            {"node_index": 0, "slot_id": slot_id, "material": "Mat", "layout_revision": "ab" * 32},
            {"Mat": bytes([7]) * 16},
            node_count=1,
        )


def test_renderable_rejects_retired_single_material_override():
    with pytest.raises(PakError, match="material_overrides"):
        _pack_renderable_record(
            {"node_index": 0, "geometry": "Geo", "material": "Mat"},
            {"Geo": bytes([8]) * 16},
            node_count=1,
        )


def test_cooked_emission_preserves_float32_values():
    factor = [9.7, 0.00001, 65504.0]
    record = pack_material_asset_descriptor(
        {"emissive_factor": factor}, {},
        header_builder=lambda _: bytes(ASSET_HEADER_SIZE),
    )
    assert len(record) == 363
    assert record[186:198] == struct.pack("<3f", *factor)
