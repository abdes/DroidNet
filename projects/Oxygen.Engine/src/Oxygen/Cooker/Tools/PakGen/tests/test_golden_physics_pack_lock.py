from __future__ import annotations

import hashlib
import json
from pathlib import Path

from pakgen.api import inspect_pak


def test_golden_physics_pack_checksum_lock():
    golden_dir = Path(__file__).parent / "_golden"
    pak_path = golden_dir / "scene_with_physics_sidecar_ref.pak"
    lock_path = golden_dir / "scene_with_physics_sidecar_ref.lock.json"

    assert pak_path.exists(), "Physics golden pak missing"
    assert lock_path.exists(), "Physics golden lock missing"

    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    pak_bytes = pak_path.read_bytes()
    sha256 = hashlib.sha256(pak_bytes).hexdigest()
    info = inspect_pak(pak_path)

    assert lock["file_size"] == len(pak_bytes)
    assert lock["sha256"] == sha256
    assert lock["pak_crc32"] == info["footer"]["real_crc32"]
    assert lock["physics_table_count"] == info["footer"]["tables"]["physics"]["count"]
    assert lock["asset_count"] == info["footer"]["directory"]["asset_count"]
