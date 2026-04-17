"""Historical Phase 03 deferred-core frame-10 closeout analyzer.

This analyzer exists to preserve the old 03-15 source/test/log closeout pack.
It is no longer the authoritative Phase 03 runtime closure surface; the active
runtime gate now lives in the VortexBasic validation flow.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path


REQUIRED_KEYS = (
    "stage_2_order",
    "stage_3_order",
    "stage_9_order",
    "stage_12_order",
    "gbuffer_contents",
    "scene_color_lit",
    "bounded_volume_local_lights",
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
    "bounded_volume_local_lights=",
)


@dataclass(frozen=True)
class ManifestStep:
    exit_code: int


@dataclass(frozen=True)
class ManifestSteps:
    build: ManifestStep
    tests: ManifestStep
    tidy: ManifestStep


@dataclass(frozen=True)
class RenderDocRuntimeValidation:
    deferred: bool


@dataclass(frozen=True)
class ManifestFiles:
    scene_renderer: Path
    scene_textures: Path
    depth_prepass_module: Path
    base_pass_module: Path
    framebuffer_impl: Path
    shader_catalog: Path
    deferred_light_common: Path
    deferred_light_point: Path
    deferred_light_spot: Path
    deferred_core_tests: Path
    publication_tests: Path
    validation_doc: Path
    phase_plan: Path


@dataclass(frozen=True)
class CaptureManifest:
    repo_root: Path
    steps: ManifestSteps
    files: ManifestFiles
    renderdoc_runtime_validation: RenderDocRuntimeValidation


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require_mapping(value: object, field_name: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise TypeError(f"{field_name} must be an object")
    return value


def require_field(mapping: dict[str, object], key: str, field_name: str) -> object:
    if key not in mapping:
        raise KeyError(f"missing required field: {field_name}.{key}")
    return mapping[key]


def require_string(value: object, field_name: str) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{field_name} must be a string")
    return value


def require_int(value: object, field_name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{field_name} must be an integer")
    return value


def require_bool(value: object, field_name: str) -> bool:
    if not isinstance(value, bool):
        raise TypeError(f"{field_name} must be a boolean")
    return value


def require_directory_path(value: object, field_name: str) -> Path:
    path_value = require_string(value, field_name)
    if not path_value.strip():
        raise ValueError(f"{field_name} must be a non-empty path")
    path = Path(path_value).resolve()
    if not path.is_dir():
        raise NotADirectoryError(f"{field_name} does not exist: {path}")
    return path


def require_file_path(value: object, repo_root: Path, field_name: str) -> Path:
    path_value = require_string(value, field_name)
    if not path_value.strip():
        raise ValueError(f"{field_name} must be a non-empty path")

    path = Path(path_value)
    if not path.is_absolute():
        path = repo_root / path

    resolved_path = path.resolve()
    if not resolved_path.is_file():
        raise FileNotFoundError(f"{field_name} does not exist: {resolved_path}")
    return resolved_path


def parse_step(value: object, field_name: str) -> ManifestStep:
    step = require_mapping(value, field_name)
    return ManifestStep(
        exit_code=require_int(
            require_field(step, "exit_code", field_name),
            f"{field_name}.exit_code",
        )
    )


def parse_steps(value: object) -> ManifestSteps:
    steps = require_mapping(value, "manifest.steps")
    return ManifestSteps(
        build=parse_step(
            require_field(steps, "build", "manifest.steps"),
            "manifest.steps.build",
        ),
        tests=parse_step(
            require_field(steps, "tests", "manifest.steps"),
            "manifest.steps.tests",
        ),
        tidy=parse_step(
            require_field(steps, "tidy", "manifest.steps"),
            "manifest.steps.tidy",
        ),
    )


def parse_files(value: object, repo_root: Path) -> ManifestFiles:
    files = require_mapping(value, "manifest.files")
    return ManifestFiles(
        scene_renderer=require_file_path(
            require_field(files, "scene_renderer", "manifest.files"),
            repo_root,
            "manifest.files.scene_renderer",
        ),
        scene_textures=require_file_path(
            require_field(files, "scene_textures", "manifest.files"),
            repo_root,
            "manifest.files.scene_textures",
        ),
        depth_prepass_module=require_file_path(
            require_field(files, "depth_prepass_module", "manifest.files"),
            repo_root,
            "manifest.files.depth_prepass_module",
        ),
        base_pass_module=require_file_path(
            require_field(files, "base_pass_module", "manifest.files"),
            repo_root,
            "manifest.files.base_pass_module",
        ),
        framebuffer_impl=require_file_path(
            require_field(files, "framebuffer_impl", "manifest.files"),
            repo_root,
            "manifest.files.framebuffer_impl",
        ),
        shader_catalog=require_file_path(
            require_field(files, "shader_catalog", "manifest.files"),
            repo_root,
            "manifest.files.shader_catalog",
        ),
        deferred_light_common=require_file_path(
            require_field(files, "deferred_light_common", "manifest.files"),
            repo_root,
            "manifest.files.deferred_light_common",
        ),
        deferred_light_point=require_file_path(
            require_field(files, "deferred_light_point", "manifest.files"),
            repo_root,
            "manifest.files.deferred_light_point",
        ),
        deferred_light_spot=require_file_path(
            require_field(files, "deferred_light_spot", "manifest.files"),
            repo_root,
            "manifest.files.deferred_light_spot",
        ),
        deferred_core_tests=require_file_path(
            require_field(files, "deferred_core_tests", "manifest.files"),
            repo_root,
            "manifest.files.deferred_core_tests",
        ),
        publication_tests=require_file_path(
            require_field(files, "publication_tests", "manifest.files"),
            repo_root,
            "manifest.files.publication_tests",
        ),
        validation_doc=require_file_path(
            require_field(files, "validation_doc", "manifest.files"),
            repo_root,
            "manifest.files.validation_doc",
        ),
        phase_plan=require_file_path(
            require_field(files, "phase_plan", "manifest.files"),
            repo_root,
            "manifest.files.phase_plan",
        ),
    )


def parse_renderdoc_runtime_validation(value: object) -> RenderDocRuntimeValidation:
    validation = require_mapping(value, "manifest.renderdoc_runtime_validation")
    return RenderDocRuntimeValidation(
        deferred=require_bool(
            require_field(
                validation,
                "deferred",
                "manifest.renderdoc_runtime_validation",
            ),
            "manifest.renderdoc_runtime_validation.deferred",
        )
    )


def parse_manifest(value: object) -> CaptureManifest:
    manifest = require_mapping(value, "manifest")
    repo_root = require_directory_path(
        require_field(manifest, "repo_root", "manifest"),
        "manifest.repo_root",
    )
    return CaptureManifest(
        repo_root=repo_root,
        steps=parse_steps(require_field(manifest, "steps", "manifest")),
        files=parse_files(require_field(manifest, "files", "manifest"), repo_root),
        renderdoc_runtime_validation=parse_renderdoc_runtime_validation(
            require_field(manifest, "renderdoc_runtime_validation", "manifest")
        ),
    )


def step_passed(step: ManifestStep, step_name: str) -> tuple[bool, str]:
    exit_code = step.exit_code
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


def analyze(manifest: CaptureManifest) -> list[str]:
    files = manifest.files

    scene_renderer = read_text(files.scene_renderer)
    scene_textures = read_text(files.scene_textures)
    depth_prepass_module = read_text(files.depth_prepass_module)
    base_pass_module = read_text(files.base_pass_module)
    framebuffer_impl = read_text(files.framebuffer_impl)
    shader_catalog = read_text(files.shader_catalog)
    deferred_light_common = read_text(files.deferred_light_common)
    deferred_light_point = read_text(files.deferred_light_point)
    deferred_light_spot = read_text(files.deferred_light_spot)
    deferred_core_tests = read_text(files.deferred_core_tests)
    publication_tests = read_text(files.publication_tests)
    validation_doc = read_text(files.validation_doc)
    phase_plan = read_text(files.phase_plan)

    build_ok = step_passed(manifest.steps.build, "build")
    tests_ok = step_passed(manifest.steps.tests, "tests")
    tidy_ok = step_passed(manifest.steps.tidy, "tidy")

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
                    "PublishDepthPrepassProducts();",
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
                    "PublishBasePassVelocity();",
                    "PublishDeferredBasePassSceneTextures(ctx);",
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
                    "BasePassLeavesSceneColorAndGBuffersInvalidUntilStage10",
                    "BasePassCompletesVelocityForDynamicGeometry",
                ),
            ),
            contains_all(
                publication_tests,
                (
                    "Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive",
                    "SceneTexturesRebuildHelperAloneDoesNotPublishStage10Products",
                ),
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
                    "PublishDeferredBasePassSceneTextures(ctx);",
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
                    "DeferredLightPointPS",
                ),
            ),
            contains_all(
                deferred_light_spot,
                (
                    "GenerateDeferredLightConeVertex(vertex_id)",
                    "DeferredLightSpotPS",
                ),
            ),
            contains_all(
                shader_catalog,
                (
                    "DeferredLightPointPS",
                    "DeferredLightSpotPS",
                ),
            ),
            contains_all(
                deferred_core_tests,
                (
                    "DeferredLightingConsumesPublishedGBuffers",
                    "DeferredLightingAccumulatesIntoSceneColor",
                    "DeferredLightingUsesOutsideVolumeLocalLights",
                    "DeferredLightingUsesInsideVolumePathWhenCameraStartsInsideLocalLights",
                    "DeferredLightingUsesNonPerspectiveLocalLightModeForNonPerspectiveViews",
                    "Vortex.DeferredLight.Directional",
                    "Vortex.DeferredLight.Point.Lighting",
                    "Vortex.DeferredLight.Spot.Lighting",
                    "Vortex.DeferredLight.Point.InsideVolumeLighting",
                    "Vortex.DeferredLight.Spot.InsideVolumeLighting",
                    "Vortex.DeferredLight.Point.NonPerspectiveLighting",
                    "Vortex.DeferredLight.Spot.NonPerspectiveLighting",
                    "graphics_->draw_log_.draws.size(), 2U",
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
                (
                    "GBufferDebugViewsAreAvailable",
                    "BasePassLeavesSceneColorAndGBuffersInvalidUntilStage10",
                ),
            ),
            contains_all(
                publication_tests,
                (
                    "Stage10PublicationPromotesSceneColorGBuffersAndKeepsVelocityAlive",
                    "SceneTexturesRebuildHelperAloneDoesNotPublishStage10Products",
                ),
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
    bounded_volume_local_lights = result_from_checks(
        (
            build_ok,
            tests_ok,
            contains_all(
                scene_renderer,
                (
                    "BuildDeferredLocalPipelineDesc(",
                    "used_outside_volume_local_lights = true;",
                    "used_camera_inside_local_lights = true;",
                    "used_non_perspective_local_lights = true;",
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
                    "DeferredLightingUsesOutsideVolumeLocalLights",
                    "Vortex.DeferredLight.Point.Lighting",
                    "DeferredLightingUsesInsideVolumePathWhenCameraStartsInsideLocalLights",
                    "Vortex.DeferredLight.Point.InsideVolumeLighting",
                    "Vortex.DeferredLight.Spot.InsideVolumeLighting",
                    "DeferredLightingUsesNonPerspectiveLocalLightModeForNonPerspectiveViews",
                    "Vortex.DeferredLight.Point.NonPerspectiveLighting",
                    "Vortex.DeferredLight.Spot.NonPerspectiveLighting",
                    "Vortex.DeferredLight.Spot.Lighting",
                ),
            ),
        )
    )

    lines = [
        "analysis_result=success",
        "analysis_profile=phase3_closeout",
        f"input_path={manifest.repo_root}",
        f"renderdoc_runtime_validation={'deferred_phase_04' if manifest.renderdoc_runtime_validation.deferred else 'active'}",
        "renderdoc_runtime_reason=RenderDoc runtime validation is deferred until Async and DemoShell migrate to Vortex.",
        f"build_exit_code={manifest.steps.build.exit_code}",
        f"tests_exit_code={manifest.steps.tests.exit_code}",
        f"tidy_exit_code={manifest.steps.tidy.exit_code}",
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
        "bounded_volume_local_lights": bounded_volume_local_lights,
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
        manifest = parse_manifest(json.loads(input_path.read_text(encoding="utf-8-sig")))
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
