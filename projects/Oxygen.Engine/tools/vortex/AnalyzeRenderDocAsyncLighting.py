"""Export Async lighting controls without requiring a fresh shadow-map raster."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shadows"))
from renderdoc_ui_analysis import (
    collect_action_records,
    renderdoc_module,
    resource_id_to_name,
    run_ui_script,
)


def build_report(controller, report, capture_path, report_path):
    rd = renderdoc_module()
    actions = collect_action_records(controller)
    names = resource_id_to_name(controller)
    tone = [
        action for action in actions
        if action.flags & rd.ActionFlags.Drawcall
        and "Vortex.PostProcess.Tonemap" in action.path
    ]
    if len(tone) != 1:
        raise RuntimeError("Expected one Async tonemap draw")
    controller.SetFrameEvent(tone[0].event_id, True)
    output = controller.GetPipelineState().GetOutputTargets()[0]
    sub = rd.Subresource()
    sub.mip, sub.slice = output.firstMip, output.firstSlice
    minimum, maximum = controller.GetMinMax(output.resource, sub, rd.CompType.Typeless)
    report.append("scene_rgb_min={}".format(list(minimum.floatValue[:3])))
    report.append("scene_rgb_max={}".format(list(maximum.floatValue[:3])))
    image_path = report_path.with_suffix(".png")
    save = rd.TextureSave()
    save.resourceId = output.resource
    save.destType = rd.FileType.PNG
    save.mip = sub.mip
    save.slice.sliceIndex = sub.slice
    save.alpha = rd.AlphaMapping.Preserve
    controller.SaveTexture(save, str(image_path))
    if not image_path.is_file():
        raise RuntimeError("Scene-color export failed")
    report.append("scene_color={}".format(image_path))

    spot_draws = [
        action for action in actions
        if action.flags & rd.ActionFlags.Drawcall
        and "Vortex.Stage12.SpotLight" in action.path
    ]
    report.append("spot_draw_count={}".format(len(spot_draws)))
    if spot_draws:
        controller.SetFrameEvent(spot_draws[-1].event_id, True)
        # A cached map is valid even when this frame contains no depth raster.
        for texture in controller.GetTextures():
            if "SharedSpotShadows" not in names.get(str(texture.resourceId), ""):
                continue
            minimum, maximum = controller.GetMinMax(
                texture.resourceId, rd.Subresource(), rd.CompType.Depth
            )
            report.append("spot_depth_range={},{}".format(
                minimum.floatValue[0], maximum.floatValue[0]))
    report.append("capture={}".format(capture_path))
    report.append("analysis_result=pass")


if __name__ == "__main__":
    run_ui_script("_async_lighting.txt", build_report)
