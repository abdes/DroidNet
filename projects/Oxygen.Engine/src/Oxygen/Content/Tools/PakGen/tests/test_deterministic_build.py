import json, hashlib
from pathlib import Path
from pakgen.api import BuildOptions, build_pak

SPEC_CONTENT = {
    "version": 1,
    "content_version": 1,
    "buffers": [],
    "textures": [],
    "audios": [],
    "materials": [
        {"name": "matA", "asset_key": "aa" * 16},
        {"name": "matB", "asset_key": "bb" * 16},
    ],
    "geometries": [],
}


def write_spec(path: Path):
    path.write_text(json.dumps(SPEC_CONTENT))


def build_det(path: Path, out: Path, manifest: Path):
    opts = BuildOptions(
        input_spec=path,
        output_path=out,
        manifest_path=manifest,
        deterministic=True,
    )
    build_pak(opts)


def test_deterministic_two_runs_identical(tmp_path):  # NOLINT_TEST
    spec1 = tmp_path / "spec.json"
    write_spec(spec1)
    out1 = tmp_path / "a1.pak"
    man1 = tmp_path / "a1.manifest.json"
    build_det(spec1, out1, man1)
    # Second run (copy spec again to ensure identical input)
    spec2 = tmp_path / "spec2.json"
    write_spec(spec2)
    out2 = tmp_path / "a2.pak"
    man2 = tmp_path / "a2.manifest.json"
    build_det(spec2, out2, man2)

    b1 = out1.read_bytes()
    b2 = out2.read_bytes()
    assert b1 == b2, "Deterministic build bytes differ"
    h1 = hashlib.sha256(b1).hexdigest()
    h2 = hashlib.sha256(b2).hexdigest()
    assert h1 == h2

    m1 = json.loads(man1.read_text())
    m2 = json.loads(man2.read_text())
    # spec_hash / pak_crc / sha256 should all match
    assert m1["spec_hash"] == m2["spec_hash"]
    assert m1["pak_crc32"] == m2["pak_crc32"]
    assert m1["sha256"] == m2["sha256"]
