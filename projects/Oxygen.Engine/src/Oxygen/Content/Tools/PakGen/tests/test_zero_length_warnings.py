import json
from pathlib import Path

from pakgen.api import BuildOptions, build_pak


def test_manifest_includes_zero_length_warnings(tmp_path: Path):
    # Arrange: spec with two buffers, first zero-length (allowed), second also zero-length (should trigger warning)
    spec = {
        "version": 1,
        "name": "ZeroLenWarnings",
        "buffers": [
            {"name": "default", "data": ""},
            {"name": "empty_extra", "data": ""},
        ],
    }
    spec_path = tmp_path / "Spec.yaml"
    import yaml

    spec_path.write_text(yaml.safe_dump(spec))

    pak_path = tmp_path / "out.pak"
    manifest_path = tmp_path / "out.manifest.json"

    # Act
    build_pak(
        BuildOptions(
            input_spec=spec_path,
            output_path=pak_path,
            manifest_path=manifest_path,
            deterministic=True,
        )
    )

    # Assert
    manifest = json.loads(manifest_path.read_text())
    assert "zero_length_resources" in manifest
    zl = manifest["zero_length_resources"]
    assert any(r["name"] == "empty_extra" for r in zl)
    assert "warnings" in manifest
    warnings = manifest["warnings"]
    assert any("empty_extra" in w for w in warnings)
