import importlib.util
from pathlib import Path
from types import SimpleNamespace


REPO_ROOT = Path(__file__).resolve().parents[3]


def load_module(name: str, relative_path: str):
    module_path = REPO_ROOT / relative_path
    spec = importlib.util.spec_from_file_location(name, module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def make_record(event_id: int, name: str, flags, path: str = ""):
    return SimpleNamespace(event_id=event_id, name=name, flags=flags, path=path)


def test_capture_compositing_activity_keeps_copy_only_draw_count_truthful(
    monkeypatch,
):
    module = load_module(
        "async_capture_analysis",
        "tools/vortex/AnalyzeRenderDocAsyncCapture.py",
    )
    monkeypatch.setattr(module, "is_work_action", lambda flags: flags == "work")

    summary = module.summarize_compositing_activity(
        [
            make_record(
                10,
                "ID3D12GraphicsCommandList::CopyTextureRegion()",
                "work",
            ),
            make_record(11, "Marker", "idle"),
        ]
    )

    assert summary["work_count"] == 1
    assert summary["draw_count"] == 0


def test_products_stage22_tonemap_fallback_uses_compositing_action_event():
    module = load_module(
        "async_products_analysis",
        "tools/vortex/AnalyzeRenderDocAsyncProducts.py",
    )

    resolved = module.resolve_stage22_tonemap_record(
        [
            make_record(15, module.DRAW_NAME, 0),
            make_record(25, module.DRAW_NAME, 0),
            make_record(35, module.DRAW_NAME, 0),
        ],
        None,
        make_record(10, "Vortex.Stage15.Fog", 0),
        make_record(30, "Vortex.CompositingTask[label=Composite Copy View 1]", 0),
    )

    assert resolved is not None
    assert resolved.event_id == 25


def test_products_stage22_visibility_does_not_alias_final_present():
    module = load_module(
        "async_products_visibility",
        "tools/vortex/AnalyzeRenderDocAsyncProducts.py",
    )

    visibility = module.resolve_stage22_visibility(
        sampled_tonemap_output_nonzero=False,
        final_present_nonzero=True,
    )

    assert visibility["stage22_tonemap_output_nonzero"] is False
    assert visibility["final_present_nonzero"] is True


def test_products_texture_sample_keys_do_not_override_stage22_verdict():
    module = load_module(
        "async_products_samples",
        "tools/vortex/AnalyzeRenderDocAsyncProducts.py",
    )

    class Report:
        def __init__(self):
            self.lines = []

        def append(self, line):
            self.lines.append(line)

    report = Report()
    module.append_texture_sample(
        report,
        "stage22_tonemap_output",
        {
            "name": "Async.SceneColor",
            "width": 1,
            "height": 1,
            "mip": 0,
            "slice": 0,
            "min": [0.0, 0.0, 0.0, 1.0],
            "max": [0.0, 0.0, 0.0, 1.0],
            "center": [0.0, 0.0, 0.0, 1.0],
            "probes": {},
            "nonzero": True,
            "rgb_nonzero": False,
        },
    )

    assert "stage22_tonemap_output_nonzero=true" not in report.lines
    assert "stage22_tonemap_output_sample_nonzero=true" in report.lines
    assert "stage22_tonemap_output_sample_rgb_nonzero=false" in report.lines
