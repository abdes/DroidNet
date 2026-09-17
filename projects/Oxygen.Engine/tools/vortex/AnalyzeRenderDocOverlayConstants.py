"""Validate per-view grid matrices and frame-varying wireframe payloads."""

import math
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script,
)


def matrix(values):
    return [[values[column * 4 + row] for column in range(4)] for row in range(4)]


def multiply(left, right):
    return [[sum(left[row][k] * right[k][column] for k in range(4))
             for column in range(4)] for row in range(4)]


def build_report(controller, report, capture_path, report_path):
    mode = os.environ.get("OXYGEN_RENDERDOC_PASS_NAME")
    if mode not in ("OverlayFamily", "OverlaySourceLoss"):
        raise RuntimeError("PassName must be OverlayFamily or OverlaySourceLoss")
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    grid_targets, wire_targets, payloads = set(), set(), set()
    for action in collect_action_records(controller):
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        grid = "Vortex.Stage20.GroundGrid" in action.path
        wire = "Vortex.Stage20.WireframeOverlay" in action.path or "Vortex.Stage9.BasePass.Wireframe" in action.path
        if not grid and not wire:
            continue
        controller.SetFrameEvent(action.event_id, True)
        pipeline = controller.GetPipelineState()
        views = {}
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
            for block in pipeline.GetConstantBlocks(stage):
                d = block.descriptor
                if names.get(str(d.resource), "").startswith("ViewConstants_View"):
                    views[(str(d.resource), d.byteOffset)] = d
        if len(views) != 1:
            raise RuntimeError(f"Missing/ambiguous ViewConstants at {action.event_id}")
        d = next(iter(views.values()))
        view_data = bytes(controller.GetBufferData(d.resource, d.byteOffset, 256))
        frame = struct.unpack_from("<Q", view_data)[0]
        view_values = struct.unpack("<64f", view_data)
        size = 208 if grid else 32
        records = [item.descriptor for item in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel, True)
                   if item.descriptor.elementByteSize == size and item.descriptor.byteSize == size]
        if len(records) != 1:
            raise RuntimeError(f"Missing structured overlay payload at {action.event_id}")
        record = records[0]
        key = (str(record.resource), record.byteOffset, size)
        # Multiple mesh draws in one wireframe batch intentionally share a payload.
        if key in payloads:
            continue
        payloads.add(key)
        values = struct.unpack("<" + "f" * (size // 4), bytes(
            controller.GetBufferData(record.resource, record.byteOffset, size)))
        if not all(math.isfinite(value) for value in values):
            raise RuntimeError("Nonfinite overlay constants")
        target = str(pipeline.GetOutputTargets()[0].resource)
        if grid:
            rotation = matrix(view_values[4:20])
            for row in range(4):
                rotation[row][3] = 1 if row == 3 else 0
            product = multiply(multiply(matrix(view_values[20:36]), rotation), matrix(values[:16]))
            error = max(abs(product[row][column] - int(row == column))
                        for row in range(4) for column in range(4))
            if error > 2e-4:
                raise RuntimeError(f"Grid/view inverse mismatch at {action.event_id}: {error}")
            grid_targets.add(target)
            report.append(f"grid_event={action.event_id} frame={frame} matrix_identity_error={error}")
        else:
            expected = ((.8, .2, .2, 1), (.2, .8, .2, 1), (.2, .2, .8, 1))[frame % 3]
            if max(abs(actual - wanted) for actual, wanted in zip(values[:4], expected)) > 1e-6:
                raise RuntimeError(f"Wireframe read another frame's color: {values[:4]} vs {expected}")
            expected_domain = 0 if "Vortex.Stage20.WireframeOverlay" in action.path else 1
            if values[4] != expected_domain:
                raise RuntimeError("Wireframe exposure-domain flag disagrees with its target stage")
            wire_targets.add(target)
            report.append(f"wire_event={action.event_id} frame={frame} color={values[:4]} pre_exposed={values[4]}")
    required_grid, required_wire = (3, 4) if mode == "OverlayFamily" else (1, 0)
    if len(grid_targets) < required_grid or len(wire_targets) < required_wire:
        raise RuntimeError(f"Missing overlay coverage: grid={len(grid_targets)}, wire={len(wire_targets)}")
    report.append(f"grid_views={len(grid_targets)} wireframe_views={len(wire_targets)}")
    report.append("overlay_constants_verdict=pass")


if __name__ == "__main__":
    run_ui_script("_overlay_constants.txt", build_report)
