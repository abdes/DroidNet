"""Export mixed-scene base-pass and translucent contributions before exposure."""

import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from AnalyzeRenderDocMultiViewExposure import build_report as inspect_views
from renderdoc_ui_analysis import collect_action_records, renderdoc_module, resource_id_to_name, run_ui_script


def build_report(controller, report, capture, path):
    inspect_views(controller, report, capture, path)
    output = Path(path).with_suffix(".json")
    result = json.loads(output.read_text())
    rd = renderdoc_module()
    names = resource_id_to_name(controller)
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    actions = collect_action_records(controller)
    for view in result["views"]:
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall
                 and view["previous_tonemap_event"] < a.event_id < view["event"]]
        base = [a for a in draws if "Vortex.Stage9.BasePass" in a.path]
        translucent = [a for a in draws if "Vortex.Stage18.Translucency" in a.path]
        if not base or not translucent:
            raise RuntimeError(f"Missing mixed material passes for {view['target_name']}")
        controller.SetFrameEvent(base[-1].event_id, True)
        targets = controller.GetPipelineState().GetOutputTargets()
        scene = [t.resource for t in targets if "SceneColor" in names.get(str(t.resource), "")]
        if len(scene) != 1:
            raise RuntimeError("Base-pass SceneColor output is not unique")
        desc = textures[str(scene[0])]
        if desc.format.compCount != 4 or desc.format.compByteWidth != 4:
            raise RuntimeError("Mixed accumulation must remain RGBA32F")
        # Bindings belong to the draw event. A raw texture download at that
        # event can still observe its pre-draw contents; the following marker
        # is an unambiguous completed-pass boundary (verified by the probe).
        base_end = next(a.event_id for a in actions if a.event_id > base[-1].event_id)
        trans_end = next(a.event_id for a in actions if a.event_id > translucent[-1].event_id)
        controller.SetFrameEvent(base_end, True)
        stem = Path(path).with_name(f"{Path(path).stem}-view-{view['index']}")

        def export(resource, label):
            file = stem.with_name(stem.name + "-" + label + ".bin")
            file.write_bytes(bytes(controller.GetTextureData(resource, rd.Subresource())))
            return str(file)

        evidence = {"base_event": base[-1].event_id,
                    "base_snapshot_event": base_end,
                    "translucent_snapshot_event": trans_end,
                    "translucent_events": [a.event_id for a in translucent],
                    "base_scene": export(scene[0], "base-scene")}
        if view["shading_path"] == "deferred":
            for label, suffix in (("base_color", "GBufferBaseColor"),
                                  ("masked_coverage", "GBufferCustomData")):
                matches = [t.resource for t in targets if names.get(str(t.resource), "").endswith(suffix)]
                if len(matches) != 1:
                    raise RuntimeError(f"Missing {suffix}")
                evidence[label] = export(matches[0], label)
        controller.SetFrameEvent(translucent[0].event_id - 1, True)
        evidence["before_translucency"] = export(scene[0], "before-translucency")
        controller.SetFrameEvent(trans_end, True)
        evidence["after_translucency"] = export(scene[0], "after-translucency")
        view["material_evidence"] = evidence
    output.write_text(json.dumps(result, indent=2) + "\n")
    report.append("mixed_material_export=pass")


if __name__ == "__main__" or "pyrenderdoc" in globals():
    run_ui_script("_mixed_materials.txt", build_report)
