import json
from pathlib import Path
from pakgen.api import build_pak, BuildOptions


def test_resource_index_map_in_manifest(tmp_path: Path):
    spec = {
        "version": 1,
        "content_version": 1,
        # Intentional non-alpha order and duplicate-like prefixes
        "buffers": [
            {"name": "__fallback", "data": "", "size": 0, "usage": 0},
            {
                "name": "z_buf",
                "data": "00",
                "usage": 1,
                "stride": 1,
                "format": 0,
            },
            {
                "name": "a_buf",
                "data": "00",
                "usage": 1,
                "stride": 1,
                "format": 0,
            },
        ],
        "textures": [
            {
                "name": "texB",
                "width": 1,
                "height": 1,
                "format": 30,
                "texture_type": 3,
                "compression_type": 0,
                "data": "ff",
            },
            {
                "name": "texA",
                "width": 1,
                "height": 1,
                "format": 30,
                "texture_type": 3,
                "compression_type": 0,
                "data": "ff",
            },
        ],
        "audios": [],
        "assets": [],
    }
    import yaml

    spec_path = tmp_path / "spec.yaml"
    spec_path.write_text(yaml.safe_dump(spec))
    out = tmp_path / "out.pak"
    manifest = tmp_path / "out.manifest.json"
    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=out,
            manifest_path=manifest,
            deterministic=True,
        )
    )
    m = json.loads(manifest.read_text())
    rim = m.get("resource_index_map")
    assert rim, "resource_index_map missing"
    # Buffers should preserve spec order (no sorting)
    buf_names = [e["name"] for e in rim["buffer"]]
    assert buf_names == ["__fallback", "z_buf", "a_buf"], buf_names
    # Textures should be sorted (deterministic) unless design changes; current planner sorts by name
    tex_names = [e["name"] for e in rim["texture"]]
    assert tex_names == sorted(tex_names), tex_names
    # Indices must be sequential starting at 0
    for key, entries in rim.items():
        indices = [e["index"] for e in entries]
        assert indices == list(range(len(indices))), (key, indices)
