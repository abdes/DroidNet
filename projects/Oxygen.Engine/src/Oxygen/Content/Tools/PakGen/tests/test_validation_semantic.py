from pakgen.spec.validator import run_validation_pipeline


def test_material_numeric_ranges():  # noqa: N802
    spec = {
        "version": 1,
        "assets": [
            {"type": "material", "name": "m0", "normal_scale": -1.0},
            {"type": "material", "name": "m1", "roughness": 11.0},
            {"type": "material", "name": "m2", "metalness": 5.0},
        ],
        "buffers": [],
        "textures": [],
        "audios": [],
    }
    errs = run_validation_pipeline(spec)
    codes = {e.code for e in errs}
    assert "E_RANGE" in codes
    # Ensure no duplicate error explosion
    assert sum(1 for e in errs if e.code == "E_RANGE") >= 2


def test_texture_mip_validation():  # noqa: N802
    spec = {
        "version": 1,
        "textures": [
            {"name": "t0", "width": 8, "height": 8, "mip_levels": 10},
            {"name": "t1", "width": 16, "height": 8, "mip_levels": 5},
        ],
        "assets": [],
        "buffers": [],
        "audios": [],
    }
    errs = run_validation_pipeline(spec)
    paths = {e.path for e in errs if e.code == "E_RANGE"}
    assert "textures[0].mip_levels" in paths
    assert "textures[1].mip_levels" not in paths
