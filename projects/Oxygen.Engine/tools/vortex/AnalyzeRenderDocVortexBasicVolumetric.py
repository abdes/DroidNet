"""RenderDoc UI analyzer for the focused VortexBasic volumetric-fog proof."""

import builtins
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

    raise RuntimeError("Unable to locate tools/shadows from the RenderDoc script path.")


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


REPORT_SUFFIX = "_vortexbasic_volumetric_report.txt"
DISPATCH_NAME = "ID3D12GraphicsCommandList::Dispatch()"
DRAW_NAME = "ID3D12GraphicsCommandList::DrawInstanced()"
VOLUMETRIC_SCOPE = "Vortex.Stage14.VolumetricFog"
FOG_SCOPE = "Vortex.Stage15.Fog"
INTEGRATED_LIGHT_SCATTERING_TOKEN = "vortex.environment.integratedlightscattering"


def records_with_name(action_records, name):
    return [record for record in action_records if record.name == name]


def records_under_prefix(action_records, prefix):
    prefix_with_sep = prefix + " > "
    return [record for record in action_records if record.path.startswith(prefix_with_sep)]


def collect_event_ids(scope_records, child_records):
    event_ids = {record.event_id for record in child_records}
    event_ids.update(record.event_id for record in scope_records)
    return event_ids


def resource_used_in_events(controller, resource_id, event_ids):
    for usage in controller.GetUsage(resource_id):
        if safe_getattr(usage, "eventId") in event_ids:
            return True
    return False


def find_named_resource_usage(controller, resource_records, event_ids, *tokens):
    matches = []
    for resource in resource_records:
        resource_id = safe_getattr(resource, "resourceId")
        name = safe_getattr(resource, "name", "")
        if resource_id is None or not name:
            continue
        lower_name = name.lower()
        if not all(token.lower() in lower_name for token in tokens):
            continue
        if resource_used_in_events(controller, resource_id, event_ids):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def bound_named_resources(controller, rd, resource_names, event_id, shader_stage, read_write, *tokens):
    controller.SetFrameEvent(event_id, True)
    state = controller.GetPipelineState()
    if read_write:
        descriptors = state.GetReadWriteResources(shader_stage, True)
    else:
        descriptors = state.GetReadOnlyResources(shader_stage, True)

    matches = []
    for used_descriptor in descriptors:
        descriptor = used_descriptor.descriptor
        resource_id = safe_getattr(descriptor, "resource")
        if resource_id is None:
            continue
        name = resource_names.get(str(resource_id), str(resource_id))
        lower_name = str(name).lower()
        if all(token.lower() in lower_name for token in tokens):
            matches.append({"resource_id": resource_id, "name": name})
    return matches


def append_bool(report, key, value):
    report.append("{}={}".format(key, str(bool(value)).lower()))


def build_report(controller, report: ReportWriter, capture_path: Path, report_path: Path):
    rd = renderdoc_module()
    action_records = collect_action_records(controller)
    resource_records = collect_resource_records_raw(controller)
    resource_names = resource_id_to_name(controller)

    volumetric_scope = records_with_name(action_records, VOLUMETRIC_SCOPE)
    volumetric_records = records_under_prefix(action_records, VOLUMETRIC_SCOPE)
    fog_scope = records_with_name(action_records, FOG_SCOPE)
    fog_records = records_under_prefix(action_records, FOG_SCOPE)

    volumetric_event_ids = collect_event_ids(volumetric_scope, volumetric_records)
    fog_event_ids = collect_event_ids(fog_scope, fog_records)

    volumetric_dispatch_count = len(records_with_name(volumetric_records, DISPATCH_NAME))
    fog_draw_count = len(records_with_name(fog_records, DRAW_NAME))
    volumetric_dispatch = (
        records_with_name(volumetric_records, DISPATCH_NAME)[0]
        if volumetric_dispatch_count > 0
        else None
    )
    fog_draw = records_with_name(fog_records, DRAW_NAME)[0] if fog_draw_count > 0 else None
    volumetric_scope_present = len(volumetric_scope) == 1
    volumetric_dispatch_valid = volumetric_dispatch_count == 1
    fog_scope_present = len(fog_scope) == 1
    fog_draw_valid = fog_draw_count == 1
    volumetric_before_fog = (
        volumetric_scope_present
        and fog_scope_present
        and volumetric_scope[0].event_id < fog_scope[0].event_id
    )

    written = (
        bound_named_resources(
            controller,
            rd,
            resource_names,
            volumetric_dispatch.event_id,
            rd.ShaderStage.Compute,
            True,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
        if volumetric_dispatch is not None
        else []
    )
    consumed = (
        bound_named_resources(
            controller,
            rd,
            resource_names,
            fog_draw.event_id,
            rd.ShaderStage.Pixel,
            False,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
        if fog_draw is not None
        else []
    )
    if not written:
        written = find_named_resource_usage(
            controller,
            resource_records,
            volumetric_event_ids,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
    if not consumed:
        consumed = find_named_resource_usage(
            controller,
            resource_records,
            fog_event_ids,
            INTEGRATED_LIGHT_SCATTERING_TOKEN,
        )
    integrated_light_scattering_written = len(written) > 0
    integrated_light_scattering_consumed_by_fog = len(consumed) > 0

    report.append("analysis_profile=vortexbasic_volumetric_fog")
    report.append("capture_path={}".format(capture_path))
    report.append("report_path={}".format(report_path))
    report.append("total_actions={}".format(len(action_records)))
    report.append("stage14_volumetric_fog_scope_count={}".format(len(volumetric_scope)))
    report.append("stage14_volumetric_fog_dispatch_count={}".format(volumetric_dispatch_count))
    report.append("stage15_fog_scope_count={}".format(len(fog_scope)))
    report.append("stage15_fog_draw_count={}".format(fog_draw_count))
    report.append(
        "stage14_volumetric_fog_event={}".format(
            volumetric_scope[0].event_id if volumetric_scope else ""
        )
    )
    report.append(
        "stage15_fog_event={}".format(fog_scope[0].event_id if fog_scope else "")
    )
    report.append(
        "integrated_light_scattering_written_resource={}".format(
            written[0]["name"] if written else ""
        )
    )
    report.append(
        "integrated_light_scattering_consumed_resource={}".format(
            consumed[0]["name"] if consumed else ""
        )
    )

    append_bool(report, "stage14_volumetric_fog_scope_present", volumetric_scope_present)
    append_bool(report, "stage14_volumetric_fog_dispatch_valid", volumetric_dispatch_valid)
    append_bool(report, "stage15_fog_scope_present", fog_scope_present)
    append_bool(report, "stage15_fog_draw_valid", fog_draw_valid)
    append_bool(report, "volumetric_fog_before_stage15_fog", volumetric_before_fog)
    append_bool(report, "integrated_light_scattering_written", integrated_light_scattering_written)
    append_bool(
        report,
        "integrated_light_scattering_consumed_by_fog",
        integrated_light_scattering_consumed_by_fog,
    )

    overall = (
        volumetric_scope_present
        and volumetric_dispatch_valid
        and fog_scope_present
        and fog_draw_valid
        and volumetric_before_fog
        and integrated_light_scattering_written
    )
    report.append("overall_verdict={}".format("pass" if overall else "fail"))

    interesting_tokens = ("volumetric", "stage15.fog", "integratedlightscattering")
    for index, record in enumerate(
        [
            record
            for record in action_records
            if any(token in "{} {}".format(record.name, record.path).lower() for token in interesting_tokens)
        ][:80],
        start=1,
    ):
        report.append(
            "action_{:02d}=event:{} name:{} path:{}".format(
                index,
                record.event_id,
                record.name,
                record.path,
            )
        )

    for resource_key, name in sorted(resource_names.items(), key=lambda item: str(item[1])):
        if INTEGRATED_LIGHT_SCATTERING_TOKEN in str(name).lower():
            report.append("resource_{}={}".format(resource_key, name))


def main():
    run_ui_script(REPORT_SUFFIX, build_report)


if __name__ == "__main__":
    main()
