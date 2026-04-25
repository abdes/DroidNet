"""Probe VortexBasic local-fog resources in a RenderDoc capture."""

import builtins
import struct
import sys
from pathlib import Path


def resolve_script_dir():
    script_candidates = []
    current_file = globals().get("__file__")
    if current_file:
        script_candidates.append(Path(current_file))

    argv = getattr(sys, "argv", None) or getattr(sys, "orig_argv", None) or []
    for arg in argv:
        try:
            candidate = Path(arg)
        except TypeError:
            continue
        if candidate.suffix.lower() == ".py":
            script_candidates.append(candidate)

    candidates = []
    for script_candidate in script_candidates:
        resolved = script_candidate.resolve()
        candidates.append(resolved.parent)
        candidates.extend(resolved.parents)

    search_root = Path.cwd().resolve()
    candidates.append(search_root)
    candidates.extend(search_root.parents)

    seen = set()
    for candidate_root in candidates:
        normalized = str(candidate_root).lower()
        if normalized in seen:
            continue
        seen.add(normalized)

        candidate = candidate_root / "tools" / "shadows"
        if (candidate / "renderdoc_ui_analysis.py").exists():
            return candidate

    raise RuntimeError("Unable to locate tools/shadows")


if "pyrenderdoc" in globals():
    builtins.pyrenderdoc = pyrenderdoc


SCRIPT_DIR = resolve_script_dir()
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from renderdoc_ui_analysis import (  # noqa: E402
    ReportWriter,
    collect_action_records,
    collect_resource_records_raw,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
    safe_getattr,
)


REPORT_SUFFIX = "_local_fog_state_probe.txt"


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [
        record
        for record in action_records
        if record.path.startswith(prefix_with_sep)
    ]


def find_first_name(records, name):
    for record in records:
        if record.name == name:
            return record
    return None


def find_last_name_any(records, names):
    for record in reversed(records):
        if record.name in names:
            return record
    return None


def read_u32_buffer(controller, resource_id, max_words):
    raw = controller.GetBufferData(resource_id, 0, max_words * 4)
    blob = bytes(raw)
    if not blob:
        return []
    word_count = len(blob) // 4
    return list(struct.unpack("<{}I".format(word_count), blob[: word_count * 4]))


def append_stage_resources(report, controller, rd, resource_names, event_id, prefix):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    for stage_name, stage in (
        ("vs", rd.ShaderStage.Vertex),
        ("ps", rd.ShaderStage.Pixel),
        ("cs", rd.ShaderStage.Compute),
    ):
        try:
            resources = state.GetReadOnlyResources(stage, True)
        except Exception:
            resources = []
        index = 0
        for used_descriptor in resources:
            descriptor = used_descriptor.descriptor
            resource_id = safe_getattr(descriptor, "resource")
            byte_size = int(safe_getattr(descriptor, "byteSize", 0) or 0)
            byte_offset = int(safe_getattr(descriptor, "byteOffset", 0) or 0)
            if resource_id is None:
                continue
            name = resource_names.get(str(resource_id), str(resource_id))
            if "LocalFog" not in name and byte_size not in (48, 56, 64):
                continue
            report.append(
                "{}_{}_ro_{}_name={}".format(prefix, stage_name, index, name)
            )
            report.append(
                "{}_{}_ro_{}_byte_offset={}".format(prefix, stage_name, index, byte_offset)
            )
            report.append(
                "{}_{}_ro_{}_byte_size={}".format(prefix, stage_name, index, byte_size)
            )
            if byte_size > 0 and byte_size <= 256:
                raw = controller.GetBufferData(resource_id, byte_offset, byte_size)
                blob = bytes(raw)
                words = len(blob) // 4
                if words > 0:
                    values = struct.unpack("<{}I".format(words), blob[: words * 4])
                    report.append(
                        "{}_{}_ro_{}_u32={}".format(
                            prefix, stage_name, index, ",".join(str(v) for v in values)
                        )
                    )
            index += 1


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)

    stage14_records = records_under_prefix(
        action_records, "Vortex.Stage14.LocalFogTiledCulling"
    )
    stage15_records = records_under_prefix(action_records, "Vortex.Stage15.LocalFog")
    stage14_dispatch = find_first_name(stage14_records, "ID3D12GraphicsCommandList::Dispatch()")
    stage15_draw = find_last_name_any(
        stage15_records,
        {
            "ExecuteIndirect()",
            "ID3D12GraphicsCommandList::DrawInstanced()",
        },
    )

    report.append("analysis_profile=vortexbasic_local_fog_probe")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("stage14_dispatch_event={}".format(stage14_dispatch.event_id if stage14_dispatch else 0))
    report.append("stage15_local_fog_event={}".format(stage15_draw.event_id if stage15_draw else 0))

    local_fog_resources = []
    for resource in resource_records:
        name = safe_getattr(resource, "name", "")
        if "LocalFog" not in name:
            continue
        resource_id = safe_getattr(resource, "resourceId")
        local_fog_resources.append((str(resource_id), name))
    for index, (resource_id, name) in enumerate(local_fog_resources):
        report.append("local_fog_resource_{}_id={}".format(index, resource_id))
        report.append("local_fog_resource_{}_name={}".format(index, name))

    for resource in resource_records:
        name = safe_getattr(resource, "name", "")
        resource_id = safe_getattr(resource, "resourceId")
        if resource_id is None:
            continue
        if name.endswith("LocalFogTileDrawArgs"):
            values = read_u32_buffer(controller, resource_id, 16)
            report.append("tile_draw_args_values={}".format(",".join(str(v) for v in values)))
            if len(values) >= 2:
                report.append("tile_draw_instance_count={}".format(values[1]))
        elif name.endswith("LocalFogTileDrawCount"):
            values = read_u32_buffer(controller, resource_id, 4)
            report.append("tile_draw_count_values={}".format(",".join(str(v) for v in values)))
        elif name.endswith("LocalFogOccupiedTiles"):
            values = read_u32_buffer(controller, resource_id, 16)
            report.append("occupied_tile_values={}".format(",".join(str(v) for v in values)))

    if stage14_dispatch is not None:
        append_stage_resources(
            report, controller, rd, resource_names, stage14_dispatch.event_id, "stage14"
        )
    if stage15_draw is not None:
        append_stage_resources(
            report, controller, rd, resource_names, stage15_draw.event_id, "stage15"
        )


run_ui_script(REPORT_SUFFIX, build_report)
