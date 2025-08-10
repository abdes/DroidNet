from __future__ import annotations

"""Resource count boundary tests (deterministic replacement for legacy Hypothesis tests).

Covers boundary at MAX_RESOURCES_PER_TYPE +/- 1 for buffers & textures & audios.
"""
from pakgen.packing.constants import MAX_RESOURCES_PER_TYPE
from pakgen.spec.validator import run_validation_pipeline


def _make_spec(n_buf: int, n_tex: int, n_aud: int):
    return {
        "version": 1,
        "buffers": [{"name": f"b{i}"} for i in range(n_buf)],
        "textures": [{"name": f"t{i}"} for i in range(n_tex)],
        "audios": [{"name": f"a{i}"} for i in range(n_aud)],
        "materials": [],
        "geometries": [],
    }


def _codes(errs):
    return {e.code for e in errs}


def _assert_limit(rtype: str, over: bool, codes: set[str]):
    if over:
        assert (
            "E_COUNT" in codes
        ), f"Expected E_COUNT when over limit for {rtype}"
    else:
        assert (
            "E_COUNT" not in codes
        ), f"Unexpected E_COUNT at or below limit for {rtype}"


def test_buffer_count_limit():  # noqa: N802
    at = _make_spec(MAX_RESOURCES_PER_TYPE, 0, 0)
    over = _make_spec(MAX_RESOURCES_PER_TYPE + 1, 0, 0)
    assert "E_COUNT" not in _codes(run_validation_pipeline(at))
    assert "E_COUNT" in _codes(run_validation_pipeline(over))


def test_texture_count_limit():  # noqa: N802
    at = _make_spec(0, MAX_RESOURCES_PER_TYPE, 0)
    over = _make_spec(0, MAX_RESOURCES_PER_TYPE + 1, 0)
    assert "E_COUNT" not in _codes(run_validation_pipeline(at))
    assert "E_COUNT" in _codes(run_validation_pipeline(over))


def test_audio_count_limit():  # noqa: N802
    at = _make_spec(0, 0, MAX_RESOURCES_PER_TYPE)
    over = _make_spec(0, 0, MAX_RESOURCES_PER_TYPE + 1)
    assert "E_COUNT" not in _codes(run_validation_pipeline(at))
    assert "E_COUNT" in _codes(run_validation_pipeline(over))
