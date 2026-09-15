"""Run an analyzer before RenderDoc opens its UI, then always signal SystemExit.

qrenderdoc --python handles SystemExit by skipping CaptureContext.Begin and
performing its normal global replay/Python shutdown. No PySide or Qt API is used.
The wrapper must validate the report: the host returns zero for SystemExit even
when the analyzer failed.
"""

import sys


def main():
    report_path = None
    try:
        import os
        import runpy
        import contextlib
        from pathlib import Path

        report_path = Path(os.environ["OXYGEN_RENDERDOC_REPORT_PATH"])
        if os.environ.get("OXYGEN_RENDERDOC_AUTOMATION_MODE") != "replay":
            raise RuntimeError("Bootstrap requires explicit replay automation mode")
        script = Path(os.environ["OXYGEN_RENDERDOC_SCRIPT_PATH"])
        # File-backed Python streams avoid inherited-pipe EOF waits after a
        # timed-out replay host is terminated. No Qt or parent I/O reader is
        # involved. The wrapper supplies these paths in the child environment.
        with contextlib.ExitStack() as streams:
            for variable, redirect in (
                ("OXYGEN_RENDERDOC_STDOUT_PATH", contextlib.redirect_stdout),
                ("OXYGEN_RENDERDOC_STDERR_PATH", contextlib.redirect_stderr),
            ):
                path = os.environ.get(variable)
                if path:
                    output = streams.enter_context(open(path, "w", encoding="utf-8", buffering=1))
                    streams.enter_context(redirect(output))
            runpy.run_path(str(script), run_name="__main__")
        lines = report_path.read_text(encoding="utf-8").splitlines()
        if "execution_mode=replay" not in lines:
            raise RuntimeError("Analyzer did not finish through the shared replay entry point")
    except BaseException:
        import traceback

        failure = "analysis_result=exception\nexecution_mode=replay\nbootstrap_exception:\n" + traceback.format_exc()
        if report_path is not None:
            try:
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text(failure, encoding="utf-8")
            except BaseException:
                sys.stderr.write(failure)
        else:
            sys.stderr.write(failure)
    finally:
        # This is the supported host signal that prevents the main UI opening,
        # including when an analyzer has a syntax/import/runtime error.
        raise SystemExit(0)


if __name__ == "__main__":
    main()
