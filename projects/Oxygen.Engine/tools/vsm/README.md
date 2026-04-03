# VSM Tooling

This directory is the canonical home for **VSM-specific** RenderDoc/UI
analysis entrypoints.

Ownership split:

- `tools/shadows/`
  - shared/common RenderDoc helpers used by both conventional shadows and VSM
  - current shared pass analyzers:
    - `AnalyzeRenderDocPassTiming.py`
    - `AnalyzeRenderDocPassFocus.py`
    - `renderdoc_ui_analysis.py`
- `tools/csm/`
  - conventional-shadow-specific analyzers and PowerShell workflows
  - imports shared/common RenderDoc helpers directly from `tools/shadows/`
- `tools/vsm/`
  - VSM-specific entrypoints only
  - owns the VSM-only analyzer entrypoints:
    - `AnalyzeRenderDocCapture.py`
    - `AnalyzeRenderDocStage15Masks.py`

## RenderDoc Python constraint

RenderDoc's embedded Python may execute entry scripts **without defining
`__file__`**.

Do not "clean up" path-resolution code back to a plain `Path(__file__)`
shortcut unless you have verified the exact qrenderdoc execution mode still
provides it.
