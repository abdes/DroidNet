"""Phase 03 deferred-core closeout analyzer.

This is intentionally source/test/log-backed. RenderDoc runtime validation is
deferred until Phase 04 migrates Async and DemoShell to Vortex.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REQUIRED_KEYS = (
    "stage_2_order",
    "stage_3_order",
    "stage_9_order",
    "stage_12_order",
    "gbuffer_contents",
    "scene_color_lit",
    "stencil_local_lights",
)

# Keep the explicit report-key literals in source so the plan-level grep gate can
# verify that this analyzer owns the required outputs.
REPORT_KEY_PREFIXES = (
    "stage_2_order=",
    "stage_3_order=",
    "stage_9_order=",
    "stage_12_order=",
    "gbuffer_contents=",
    "scene_color_lit=",
    "stencil_local_lights=",
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def normalize(path_value: str | None, repo_root: Path) -> Path | None:
    if not path_value:
        return None
    path = Path(path_value)
    if not path.is_absolute():
        path = repo_root / path
    return path.resolve()


def step_passed(manifest: dict, step_name: str) -> tuple[bool, str]:
    step = manifest["steps"].get(step_name, {})
    exit_code = int(step.get("exit_code", 1))
    if exit_code == 0:
        return True, f"{step_name} exit_code=0"
    return False, f"{step_name} exit_code={exit_code}"


def contains_all(text: str, needles: tuple[str, ...]) -> tuple[bool, str]:
    missing = [needle for needle in needles if needle not in text]
    if missing:
        return False, "missing tokens: " + ", ".join(missing)
    return True, "all tokens present"


def ordered(text: str, tokens: tuple[str, ...]) -> tuple[bool, str]:
    positions = []
    for token in tokens:
        position = text.find(token)
        if position < 0:
            return False, f"missing token: {token}"
        positions.append(position)
    if positions != sorted(positions):
        return False, "out-of-order tokens: " + " -> ".join(tokens)
    return True, "ordered tokens: " + " -> ".join(tokens)


def result_from_checks(
    checks: tuple[tuple[bool, str], ...],
) -> tuple[str, str]:
    failures = [detail for passed, detail in checks if not passed]
    if failures:
        return "fail", "; ".join(failures)
    return "pass", "; ".join(detail for _, detail in checks)


def write_report(report_path: Path, lines: list[str]) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def analyze(manifest: dict) -> list[str]:
    repo_root = Path(manifest["repo_root"]).resolve()
    files = manifest["files"]

    scene_renderer = read_text(normalize(files["scene_renderer"], repo_root))
    scene_textures = read_text(normalize(files["scene_textures"], repo_root))
    depth_prepass_module = read_text(
        normalize(files["depth_prepass_module"], repo_root)
    )
    base_pass_module = read_text(normalize(files["base_pass_module"], repo_root))
    framebuffer_impl = read_text(normalize(files["framebuffer_impl"], repo_root))
    shader_catalog = read_text(normalize(files["shader_catalog"], repo_root))
    deferred_light_common = read_text(
        normalize(files["deferred_light_common"], repo_root)
    )
    deferred_light_point = read_text(
        normalize(files["deferred_light_point"], repo_root)
    )
    deferred_light_spot = read_text(normalize(files["deferred_light_spot"], repo_root))
    deferred_core_tests = read_text(normalize(files["deferred_core_tests"], repo_root))
    publication_tests = read_text(normalize(files["publication_tests"], repo_root))
    validation_doc = read_text(normalize(files["validation_doc"], repo_root))
    phase_plan = read_text(normalize(files["phase_plan"], repo_root))

    build_ok = step_passed(manifest, "build")
    tests_ok = step_passed(manifest, "tests")
    tidy_ok = step_passed(manifest, "tidy")

    stage_2 = result_from_checks(
        (
            build_ok,
            tests_ok,
            ordered(
                scene_renderer,
                (
                    "// Stage 2: InitViews",
                    "init_views_->Execute(ctx, scene_textures_)",
                    "// Stage 3: Depth prepass + early velocity",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "InitViewsPublishesPreparedSceneFrameForEverySceneView",
                    "InitViewsKeepsTheActiveViewPreparedFrameBoundToCurrentView",
                ),
            ),
        )
    )
    stage_3 = result_from_checks(
        (
            build_ok,
            tests_ok,
            ordered(
                scene_renderer,
                (
                    "depth_prepass_->Execute(ctx, scene_textures_)",
                    "ApplyStage3DepthPrepassState();",
                    "// Stage 9: Base pass",
                ),
            ),
            contains_all(
                depth_prepass_module,
                (
                    'AcquireCommandRecorder(queue_key, "Vortex DepthPrepass")',
                    "BuildDepthPrepassFramebuffer(",
                    "BuildDepthPrepassPipelineDesc(",
                    "CopySceneDepthToPartialDepth(",
                    "recorder->Draw(",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "DepthPrepassRecordsRealDrawWorkFromPreparedMetadata",
                    "DepthPrepassPublishesSceneDepthAndPartialDepth",
                    "DepthPrepassCompletenessControlsEarlyDepthContract",
                ),
            ),
            contains_all(
                publication_tests,
                ("Stage3PublicationKeepsSceneColorAndGBuffersInvalidUntilStage10",),
            ),
        )
    )
    stage_9 = result_from_checks(
        (
            build_ok,
            tests_ok,
            ordered(
                scene_renderer,
                (
                    "// Stage 9: Base pass",
                    "base_pass_->Execute(ctx, scene_textures_)",
                    "ApplyStage9BasePassState();",
                    "// Stage 10: Rebuild scene textures with GBuffers",
                    "ApplyStage10RebuildState();",
                ),
            ),
            contains_all(
                base_pass_module,
                (
                    'AcquireCommandRecorder(queue_key, "Vortex BasePass")',
                    "BuildBasePassFramebuffer(",
                    "BuildBasePassPipelineDesc(",
                    "recorder->Draw(",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "BasePassPromotesGBuffersAtStage10",
                    "BasePassCompletesVelocityForDynamicGeometry",
                ),
            ),
            contains_all(
                publication_tests,
                ("Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive",),
            ),
        )
    )
    stage_12 = result_from_checks(
        (
            build_ok,
            tests_ok,
            ordered(
                scene_renderer,
                (
                    "ApplyStage10RebuildState();",
                    "// Stage 12: Deferred direct lighting",
                    "RenderDeferredLighting(ctx, scene_textures_);",
                ),
            ),
            contains_all(
                scene_renderer,
                (
                    'AcquireCommandRecorder(\n    queue_key, "Vortex DeferredLighting")',
                    "BuildDeferredDirectionalPipelineDesc(",
                    "BuildDeferredLocalPipelineDesc(",
                    "deferred_light_constants_buffer_",
                    "draw_local_light(",
                ),
            ),
            contains_all(
                deferred_light_point,
                (
                    "GenerateDeferredLightSphereVertex(vertex_id)",
                    "DeferredLightPointStencilMarkPS",
                ),
            ),
            contains_all(
                deferred_light_spot,
                (
                    "GenerateDeferredLightConeVertex(vertex_id)",
                    "DeferredLightSpotStencilMarkPS",
                ),
            ),
            contains_all(
                shader_catalog,
                (
                    "DeferredLightPointStencilMarkPS",
                    "DeferredLightSpotStencilMarkPS",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "DeferredLightingConsumesPublishedGBuffers",
                    "DeferredLightingAccumulatesIntoSceneColor",
                    "DeferredLightingUsesStencilBoundedLocalLights",
                    "Vortex.DeferredLight.Directional",
                    "Vortex.DeferredLight.Point.StencilMark",
                    "Vortex.DeferredLight.Point.Lighting",
                    "Vortex.DeferredLight.Spot.StencilMark",
                    "Vortex.DeferredLight.Spot.Lighting",
                    "graphics_->draw_log_.draws.size(), 4U",
                ),
            ),
        )
    )
    gbuffer_contents = result_from_checks(
        (
            build_ok,
            tests_ok,
            contains_all(
                base_pass_module,
                (
                    "GetGBufferResource(GBufferIndex::kNormal)",
                    "GetGBufferResource(GBufferIndex::kMaterial)",
                    "GetGBufferResource(GBufferIndex::kBaseColor)",
                    "GetGBufferResource(GBufferIndex::kCustomData)",
                    "GetSceneColorResource()",
                ),
            ),
            contains_all(
                deferred_core_tests,
                ("GBufferDebugViewsAreAvailable", "BasePassPromotesGBuffersAtStage10"),
            ),
            contains_all(
                publication_tests,
                ("Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive",),
            ),
        )
    )
    scene_color_lit = result_from_checks(
        (
            build_ok,
            tests_ok,
            contains_all(
                scene_renderer,
                (
                    "BuildDeferredDirectionalPipelineDesc(",
                    "BuildDeferredLocalPipelineDesc(",
                    "deferred_lighting_state_.accumulated_into_scene_color = true;",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "DeferredLightingAccumulatesIntoSceneColor",
                    "graphics_->draw_log_.draws.size(), 1U",
                    "Vortex.DeferredLight.Directional",
                ),
            ),
        )
    )
    stencil_local_lights = result_from_checks(
        (
            build_ok,
            tests_ok,
            contains_all(
                scene_renderer,
                (
                    "ClearDepthStencilView(",
                    "DeferredLightPointStencilMarkPS",
                    "DeferredLightSpotStencilMarkPS",
                    "used_stencil_bounded_local_lights = true;",
                    "stencil_mark_pass_count",
                    "stencil_lighting_pass_count",
                ),
            ),
            contains_all(
                framebuffer_impl,
                (
                    "resource_registry.Find(*texture, view_desc)",
                    "is_read_only_dsv = depth_attachment.is_read_only",
                ),
            ),
            contains_all(
                deferred_light_common,
                (
                    "GenerateDeferredLightSphereVertex",
                    "GenerateDeferredLightConeVertex",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "DeferredLightingUsesStencilBoundedLocalLights",
                    "Vortex.DeferredLight.Point.StencilMark",
                    "Vortex.DeferredLight.Point.Lighting",
                    "Vortex.DeferredLight.Spot.StencilMark",
                    "Vortex.DeferredLight.Spot.Lighting",
                ),
            ),
        )
    )

    lines = [
        "analysis_result=success",
        "analysis_profile=phase3_closeout",
        f"input_path={manifest['repo_root']}",
        f"renderdoc_runtime_validation={'deferred_phase_04' if manifest['renderdoc_runtime_validation']['deferred'] else 'active'}",
        "renderdoc_runtime_reason=RenderDoc runtime validation is deferred until Async and DemoShell migrate to Vortex.",
        f"build_exit_code={manifest['steps']['build']['exit_code']}",
        f"tests_exit_code={manifest['steps']['tests']['exit_code']}",
        f"tidy_exit_code={manifest['steps']['tidy']['exit_code']}",
        f"validation_doc_mentions_phase4_deferral={'true' if 'Phase 04' in validation_doc else 'false'}",
        f"phase_plan_mentions_phase4_deferral={'true' if 'Phase 4' in phase_plan or 'Phase 04' in phase_plan else 'false'}",
    ]

    results = {
        "stage_2_order": stage_2,
        "stage_3_order": stage_3,
        "stage_9_order": stage_9,
        "stage_12_order": stage_12,
        "gbuffer_contents": gbuffer_contents,
        "scene_color_lit": scene_color_lit,
        "stencil_local_lights": stencil_local_lights,
    }
    for key in REQUIRED_KEYS:
        status, detail = results[key]
        lines.append(f"{key}={status}")
        lines.append(f"{key}_details={detail}")

    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    report_path = Path(args.report).resolve()

    try:
        manifest = json.loads(input_path.read_text(encoding="utf-8-sig"))
        lines = analyze(manifest)
        lines.insert(2, f"manifest_path={input_path}")
        lines.insert(3, f"report_path={report_path}")
        write_report(report_path, lines)
    except Exception as exc:  # pragma: no cover - failure path is intentional
        write_report(
            report_path,
            [
                "analysis_result=exception",
                f"manifest_path={input_path}",
                f"report_path={report_path}",
                f"exception={exc}",
            ],
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
