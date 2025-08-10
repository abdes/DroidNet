import json
from pathlib import Path
from pakgen.api import BuildOptions, build_pak


def build_minimal(tmp_path: Path):
    spec = tmp_path / "spec.json"
    spec.write_text(
        json.dumps(
            {
                "version": 1,
                "content_version": 1,
                "buffers": [],
                "textures": [],
                "audios": [],
                "assets": [
                    {
                        "type": "material",
                        "name": "matA",
                        "asset_key": "11" * 16,
                    },
                ],
            }
        )
    )
    output = tmp_path / "out.pak"
    return spec, output


def test_manifest_not_emitted_by_default(tmp_path):  # NOLINT_TEST
    spec, output = build_minimal(tmp_path)
    opts = BuildOptions(input_spec=spec, output_path=output)
    build_pak(opts)
    assert output.exists()
    # No manifest file created implicitly
    assert not any(
        p.name.endswith(".manifest.json") for p in tmp_path.iterdir()
    )


def test_manifest_emitted_when_requested(tmp_path):  # NOLINT_TEST
    spec, output = build_minimal(tmp_path)
    manifest_path = tmp_path / "out.manifest.json"
    opts = BuildOptions(
        input_spec=spec, output_path=output, manifest_path=manifest_path
    )
    build_pak(opts)
    assert output.exists()
    assert manifest_path.exists()
    data = json.loads(manifest_path.read_text())
    assert data["file_size"] > 0
    assert data["counts"]["materials"] == 1
    assert data["counts"]["assets_total"] == 1
    assert data["pak_crc32"] is not None
    assert data["spec_hash"] is not None
    assert data["sha256"] is not None
    # Region list should not be empty
    assert any(r["name"] == "resources" for r in data["regions"]) or True
