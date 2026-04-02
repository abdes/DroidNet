"""Flicker-local entrypoint for the shared auto-exposure RenderDoc analyzer."""

import builtins
import importlib.util
import sys
from pathlib import Path


def _script_path():
    current_file = globals().get("__file__")
    if current_file:
        return Path(current_file).resolve()

    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.name == "AnalyzeRenderDocAutoExposureDeepDive.py":
            return candidate.resolve()

    fallback = (
        Path.cwd()
        / "tools"
        / "flicker"
        / "AnalyzeRenderDocAutoExposureDeepDive.py"
    )
    if fallback.exists():
        return fallback.resolve()

    raise RuntimeError(
        "Unable to resolve AnalyzeRenderDocAutoExposureDeepDive.py"
    )


def main() -> int:
    script_path = (
        _script_path().parents[2]
        / "Examples"
        / "RenderScene"
        / "AnalyzeRenderDocAutoExposureDeepDive.py"
    )
    spec = importlib.util.spec_from_file_location(
        "_oxygen_renderdoc_auto_exposure_deep_dive", script_path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("Unable to load {}".format(script_path))

    if "pyrenderdoc" in globals():
        builtins.pyrenderdoc = pyrenderdoc

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, "main"):
        return 0

    result = module.main()
    return 0 if result is None else int(result)


if __name__ == "__main__":
    raise SystemExit(main())
