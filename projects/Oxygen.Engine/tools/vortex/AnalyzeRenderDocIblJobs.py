"""Verify partial IBL publication and labelled cube orientation in replay."""

from collections import Counter
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import renderdoc_module, run_ui_script


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    structured = controller.GetStructuredFile()
    batches = []

    def descendants(action):
        result = []
        if action.flags & rd.ActionFlags.Dispatch:
            result.append(action)
        for child in action.children:
            result.extend(descendants(child))
        return result

    def visit(action):
        if action.GetName(structured) == "Vortex.Environment.IBL.Process":
            batches.append(descendants(action))
        else:
            for child in action.children:
                visit(child)

    for action in controller.GetRootActions():
        visit(action)
    if [len(batch) for batch in batches] != [1, 174, 174, 174, 171]:
        raise RuntimeError("Expected initialization plus four partial batches")
    dispatches = [action for batch in batches for action in batch]
    counts = Counter()
    processed_cube = None
    names = {
        str(resource.resourceId): resource.name
        for resource in controller.GetResources()
    }
    for action in dispatches:
        controller.SetFrameEvent(action.eventId, True)
        shader = controller.GetPipelineState().GetShaderReflection(
            rd.ShaderStage.Compute
        )
        if not shader:
            raise RuntimeError(f"Missing shader at event {action.eventId}")
        counts[shader.entryPoint] += 1
        if shader.entryPoint == "IblNormalizeCS" and processed_cube is None:
            outputs = {
                resource.descriptor.resource
                for resource in controller.GetPipelineState().GetReadWriteResources(
                    rd.ShaderStage.Compute, True
                )
                if names.get(str(resource.descriptor.resource)) == "IBL.ProcessedCube"
            }
            if len(outputs) != 1:
                raise RuntimeError("Expected one candidate processed cube")
            processed_cube = outputs.pop()
    expected = {
        "IblInitializeCS": 1,
        "IblPrepareCS": 96,
        "IblRangeCS": 1,
        "IblNormalizeCS": 96,
        "IblMipCS": 28,
        "IblShCS": 96,
        "IblShReduceCS": 1,
        "IblPrefilterCS": 124,
        "IblNarrowCS": 248,
        "IblPrecisionRangeCS": 1,
        "IblPrecisionReduceCS": 1,
        "IblCompleteCS": 1,
    }
    if counts != expected or shader.entryPoint != "IblCompleteCS":
        raise RuntimeError(f"Unexpected producer sequence: {dict(counts)}")

    def metadata(resource):
        raw = bytes(controller.GetBufferData(resource, 0, 32))
        return raw, struct.unpack("<ffIIfIII", raw)

    owners = {}
    for resource in controller.GetResources():
        if resource.name == "IBL.Metadata":
            _, values = metadata(resource.resourceId)
            revision = values[3]
            if revision in (980, 981):
                if revision in owners:
                    raise RuntimeError(f"Duplicate generation {revision}")
                owners[revision] = resource.resourceId
    if set(owners) != {980, 981}:
        raise RuntimeError("Expected retained revision 980 and candidate 981")
    old_bytes, old_values = metadata(owners[980])
    if old_values[2] != 3:
        raise RuntimeError("Retained generation is incomplete")
    checkpoints = []
    for batch, expected_flags in zip(batches, (0, 1, 1, 1, 3)):
        event = batch[-1].eventId
        controller.SetFrameEvent(event, True)
        retained, _ = metadata(owners[980])
        _, candidate = metadata(owners[981])
        if retained != old_bytes:
            raise RuntimeError(f"Retained metadata changed at event {event}")
        if candidate[2] != expected_flags or candidate[3] != 981:
            raise RuntimeError(
                f"Incorrect candidate readiness at event {event}: {candidate}"
            )
        checkpoints.append(
            {
                "event": event,
                "dispatches": len(batch),
                "retained_revision": 980,
                "candidate_flags": candidate[2],
            }
        )
    controller.SetFrameEvent(dispatches[-2].eventId, True)
    if metadata(owners[981])[1][2] != 1:
        raise RuntimeError("Candidate became complete before its final producer")

    controller.SetFrameEvent(dispatches[-1].eventId, True)
    texture = next(
        texture
        for texture in controller.GetTextures()
        if texture.resourceId == processed_cube
    )
    if (
        texture.width,
        texture.height,
        texture.arraysize,
        texture.format.compByteWidth,
    ) != (32, 32, 6, 4):
        raise RuntimeError("Unexpected labelled-cube dimensions or precision")
    orientation_checks = 0
    maximum_error = 0.0
    for face in range(6):
        subresource = rd.Subresource()
        subresource.slice = face
        pixels = list(
            struct.iter_unpack(
                "<4f", bytes(controller.GetTextureData(processed_cube, subresource))
            )
        )
        if len(pixels) != 32 * 32:
            raise RuntimeError("Incomplete labelled-cube face")
        for index, pixel in enumerate(pixels):
            x, y = index % 32, index // 32
            # Independent labels authored by MultiFrameJobPublishesOnlyCompleteProducts.
            expected = (1.0 + face + x, 0.5 * (1.0 + y), 0.125, 1.0)
            for actual, reference in zip(pixel, expected):
                error = abs(actual - reference)
                if not math.isfinite(actual) or error > 1.0e-4:
                    raise RuntimeError(
                        f"Cube orientation mismatch at face={face}, x={x}, y={y}: {pixel}"
                    )
                maximum_error = max(maximum_error, error)
                orientation_checks += 1
    result = {
        "verdict": "pass",
        "capture": str(capture_path),
        "dispatches": len(dispatches),
        "entry_counts": dict(counts),
        "batches": checkpoints,
        "orientation_scalars": orientation_checks,
        "orientation_maximum_error": maximum_error,
        "scope": "Diagnostic four-frame job fixture; readbacks serialize GPU work. Scene scheduling, in-flight pressure and timing are separate gates.",
    }
    Path(report_path).with_suffix(".json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    report.append(
        "ibl_jobs_verdict=pass batches=5 dispatches=694 retained=980 candidate=981"
    )
    report.append(
        f"ibl_orientation_verdict=pass scalars={orientation_checks} maximum_error={maximum_error}"
    )


if __name__ == "__main__":
    run_ui_script("_ibl_jobs.txt", build_report)
