//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/CameraRigController.h"
#include "MultiView/MainModule.h"
#include "MultiView/SceneBootstrapper.h"
#include <fmt/format.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>
#include <Oxygen/Vortex/ViewFeatureProfile.h>

namespace oxygen::examples::multiview {

namespace {
  //! Names the interaction proof step shown at `frame`.
  constexpr auto InteractionPhaseName(const std::uint64_t frame)
    -> std::string_view
  {
    struct Phase {
      std::uint64_t end_frame;
      std::string_view name;
    };
    constexpr auto kPhases = std::array {
      Phase { .end_frame = 40U, .name = "Static reference" },
      Phase { .end_frame = 52U, .name = "Motion and compensation" },
      Phase { .end_frame = 60U, .name = "PiP manual exposure" },
      Phase { .end_frame = 72U, .name = "Auto, seed and cut" },
      Phase { .end_frame = 88U, .name = "Reorder, viewport and scissor" },
      Phase { .end_frame = 100U, .name = "Window resize" },
      Phase { .end_frame = 108U, .name = "Hide and reopen" },
      Phase { .end_frame = 112U, .name = "Fresh PiP lifetime" },
      Phase { .end_frame = 120U, .name = "Shared exposure" },
      Phase { .end_frame = 128U, .name = "Source removed" },
      Phase { .end_frame = 136U, .name = "Source recreated" },
    };
    for (const auto& phase : kPhases) {
      if (frame < phase.end_frame) {
        return phase.name;
      }
    }
    return "Paused final state";
  }

  constexpr auto IsMixedProof(ExposureProofScenario proof) -> bool
  {
    return proof == ExposureProofScenario::kMixed
      || proof == ExposureProofScenario::kInteractions;
  }

  constexpr auto IsLayoutExposureProof(ExposureProofScenario proof) -> bool
  {
    return proof == ExposureProofScenario::kLayouts || IsMixedProof(proof);
  }

  constexpr size_t kDefaultSceneCapacity = 128;
  constexpr uint32_t kOffscreenPreviewWidth = 512U;
  constexpr uint32_t kOffscreenPreviewHeight = 288U;
  constexpr uint32_t kOffscreenCaptureWidth = 256U;
  constexpr uint32_t kOffscreenCaptureHeight = 256U;
  constexpr std::string_view kOffscreenPreviewName
    = "M06B.OffscreenPreview.Deferred";
  constexpr std::string_view kOffscreenCaptureName
    = "M06B.OffscreenCapture.Forward";
  constexpr auto kOffscreenPreviewViewId = ViewId { 0x060B0101ULL };
  constexpr auto kOffscreenCaptureViewId = ViewId { 0x060B0102ULL };

  auto HasPositiveExtent(const platform::window::ExtentT& extent) -> bool
  {
    return extent.width > 0U && extent.height > 0U;
  }

  auto GetFramebufferExtent(const graphics::Framebuffer& framebuffer)
    -> platform::window::ExtentT
  {
    const auto& fb_desc = framebuffer.GetDescriptor();
    if (!fb_desc.color_attachments.empty()
      && fb_desc.color_attachments[0].texture) {
      const auto& color_desc
        = fb_desc.color_attachments[0].texture->GetDescriptor();
      return { .width = color_desc.width, .height = color_desc.height };
    }
    if (fb_desc.depth_attachment.texture) {
      const auto& depth_desc
        = fb_desc.depth_attachment.texture->GetDescriptor();
      return { .width = depth_desc.width, .height = depth_desc.height };
    }
    return {};
  }

  auto ResolveFramebufferColorTexture(const graphics::Framebuffer& framebuffer)
    -> std::shared_ptr<graphics::Texture>
  {
    const auto& desc = framebuffer.GetDescriptor();
    if (desc.color_attachments.empty()) {
      return {};
    }
    return desc.color_attachments.front().texture;
  }

  auto MakeLocalView(const uint32_t width, const uint32_t height) -> View
  {
    View view {};
    view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view.scissor = Scissors {
      .left = 0,
      .top = 0,
      .right = static_cast<int32_t>(width),
      .bottom = static_cast<int32_t>(height),
    };
    return view;
  }

  struct PipLayout {
    float width { 0.0F };
    float height { 0.0F };
    float offset_x { 0.0F };
    float offset_y { 0.0F };
  };

  auto ComputePipLayout(const platform::window::ExtentT& extent,
    const bool resized = false) -> PipLayout
  {
    const float kPipWidthRatio = resized ? 0.30F : 0.45F;
    const float kPipHeightRatio = resized ? 0.60F : 0.45F;
    constexpr float kPipMargin = 24.0F;

    const auto sw = static_cast<float>(extent.width);
    const auto sh = static_cast<float>(extent.height);
    const float pip_w = std::floor(sw * kPipWidthRatio);
    const float pip_h = std::floor(sh * kPipHeightRatio);
    const float offset_x = std::max(0.0F, sw - pip_w - kPipMargin);
    const float offset_y
      = std::clamp(kPipMargin, 0.0F, std::max(0.0F, sh - pip_h));

    return {
      .width = pip_w,
      .height = pip_h,
      .offset_x = offset_x,
      .offset_y = offset_y,
    };
  }

  auto CreatePerspectiveCameraNode(scene::Scene& scene, std::string_view name)
    -> scene::SceneNode
  {
    auto node = scene.CreateNode(std::string(name));
    const bool attached
      = node.AttachCamera(std::make_unique<scene::PerspectiveCamera>());
    CHECK_F(attached, "Failed to attach {}", name);
    return node;
  }

} // namespace

MainModule::MainModule(
  const host::HostContext& host, MainModuleConfig config) noexcept
  : Base(host)
  , scene_bootstrapper_(config.point_light_enabled
        && config.exposure_proof != ExposureProofScenario::kAtmosphere,
      config.spot_light_enabled
        && config.exposure_proof != ExposureProofScenario::kAtmosphere)
  , config_(std::move(config))
{

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  pip_view_id_ = Publisher().GetOrCreateViewId("PipView");
  top_view_id_ = Publisher().GetOrCreateViewId("TopView");
  debug_view_id_ = Publisher().GetOrCreateViewId("DebugView");
  shadow_view_id_ = Publisher().GetOrCreateViewId("ShadowView");
  diagnostics_view_id_ = Publisher().GetOrCreateViewId("DiagnosticsView");
}

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  constexpr uint32_t kDefaultWidth = 2560;
  constexpr uint32_t kDefaultHeight = 1400;

  auto props = Base::BuildWindowProperties();
  props.title = "Oxygen :: Examples :: MultiView";
  props.extent = platform::window::ExtentT {
    .width = kDefaultWidth,
    .height = kDefaultHeight,
  };
  return props;
}

// Attaching only builds the shell and its bindings; running out of memory there
// is not recoverable.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  CHECK_F(static_cast<bool>(engine), "MultiView requires a valid engine");
  const bool exposure_proof
    = config_.exposure_proof != ExposureProofScenario::kNone;
  if (exposure_proof) {
    engine->Pause();
  }

  // Initialize DemoShell with camera controls enabled outside proof layouts.
  auto shell = std::make_unique<DemoShell>();
  auto shell_config = MakeShellConfig();
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = !config_.feature_variant_proof_layout && !exposure_proof,
    .environment = false,
    .post_process = !config_.feature_variant_proof_layout && !exposure_proof,
    .ground_grid
    = config_.exposure_proof != ExposureProofScenario::kConsumerVisual,
  };
  shell_config.enable_camera_rig = !exposure_proof;
  if (config_.exposure_proof == ExposureProofScenario::kConsumerVisual) {
    auto plan = console::ConsoleStartupPlan {};
    plan
      .Set("vtx.local_fog.global_start_distance_m", 0.1,
        console::CVarValueOrigin::kAppForced)
      .Set("vtx.local_fog.enable", true, console::CVarValueOrigin::kAppForced)
      .Set("vtx.local_fog.render_during_height_fog_pass", true,
        console::CVarValueOrigin::kAppForced)
      .Set("vtx.volumetric_fog.jitter", config_.visual_fog_jitter,
        console::CVarValueOrigin::kAppForced);
    engine->GetConsole().ApplyStartupPlan(plan);
  }
  if (config_.exposure_proof == ExposureProofScenario::kAtmosphere
    || config_.exposure_proof == ExposureProofScenario::kAtmosphereLit
    || config_.exposure_proof == ExposureProofScenario::kConsumerVisual
    || IsLayoutExposureProof(config_.exposure_proof)
    || config_.directional_array_proof) {
    shell_config.force_environment_override = false;
    shell_config.initial_preview_sun_enabled = false;
  }

  CHECK_F(shell->Initialize(shell_config),
    "MultiView: DemoShell initialization failed");
  LOG_F(INFO, "[MultiView] DemoShell initialized (camera controls enabled)");

  shell->GetPanelCommands()->AddEnum("multiview.fog", "Visual Fog",
    "Fog of the visual check: clear, volumetric or local",
    { "clear", "volumetric", "local" }, [this](const std::size_t i) -> void {
      constexpr auto kModes = std::array {
        VisualFogMode::kClear,
        VisualFogMode::kVolumetric,
        VisualFogMode::kLocal,
      };
      pending_visual_fog_mode_ = kModes.at(i);
    });

  StageScene();
  const auto staged_scene = Stage().GetStagedScene();
  if (config_.exposure_proof == ExposureProofScenario::kAtmosphere) {
    scene_bootstrapper_.ApplyAtmosphereProof(0U);
  } else if (config_.exposure_proof == ExposureProofScenario::kAtmosphereLit) {
    scene_bootstrapper_.ApplyLitAtmosphereProof();
  } else if (config_.exposure_proof == ExposureProofScenario::kConsumerVisual) {
    scene_bootstrapper_.ApplyConsumerVisualProof(config_.visual_fog_mode);
  }
  if (IsLayoutExposureProof(config_.exposure_proof)) {
    if (IsMixedProof(config_.exposure_proof)) {
      scene_bootstrapper_.ApplyMixedExposureProof();
    } else {
      scene_bootstrapper_.ApplyConsumerVisualProof(VisualFogMode::kVolumetric);
    }
    auto& environment = *staged_scene->GetEnvironment();
    auto* post
      = environment.TryGetSystem<scene::environment::PostProcessVolume>().get();
    if (!post) {
      post = &environment.AddSystem<scene::environment::PostProcessVolume>();
    }
    auto exposure = scene::ExposureSettings {};
    exposure.key = 12.5F;
    exposure.metering_mode = IsMixedProof(config_.exposure_proof)
      ? engine::MeteringMode::kAverage
      : engine::MeteringMode::kSpot;
    post->SetExposureSettings(exposure);
  }
  if (config_.directional_array_proof) {
    scene_bootstrapper_.ApplyDirectionalArrayProof();
  }
  return shell;
}

auto MainModule::StageScene() -> void
{
  Stage().Stage(
    std::make_shared<scene::Scene>("MultiViewScene", kDefaultSceneCapacity));
  const auto staged_scene = Stage().GetStagedScene();

  main_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "MainCamera");
  pip_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "PipCamera");
  if (config_.proof_layout || config_.aux_proof_layout
    || config_.feature_variant_proof_layout) {
    top_camera_node_ = CreatePerspectiveCameraNode(*staged_scene, "TopCamera");
    debug_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "DebugCamera");
    shadow_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "ShadowCamera");
  }
  if (config_.feature_variant_proof_layout) {
    diagnostics_camera_node_
      = CreatePerspectiveCameraNode(*staged_scene, "DiagnosticsCamera");
  }
  if (config_.offscreen_proof_layout) {
    offscreen_preview_camera_node_ = CreatePerspectiveCameraNode(
      *staged_scene, "M06BOffscreenPreviewCamera");
    offscreen_capture_camera_node_ = CreatePerspectiveCameraNode(
      *staged_scene, "M06BOffscreenCaptureCamera");
  }

  Stage().SetStagedMainCamera(main_camera_node_);
  scene_bootstrapper_.BindToScene(staged_scene);
  (void)scene_bootstrapper_.EnsureSceneWithContent();
  const auto extent = ResolveRenderExtent();
  if (HasPositiveExtent(extent)) {
    UpdateCameras(extent);
    last_viewport_ = extent;
  }
}

auto MainModule::OnShutdown() noexcept -> void
{
  if (config_.offscreen_proof_layout) {
    if (auto renderer = ResolveRenderer()) {
      for (const auto view_id :
        { kOffscreenPreviewViewId, kOffscreenCaptureViewId }) {
        if (!renderer->ReleaseOffscreenViewState(view_id,
              vortex::CompositionView::ViewStateHandle { view_id.get() })) {
          LOG_F(WARNING,
            "[MultiView] Offscreen view {} could not release its state",
            view_id.get());
        }
      }
    }
  }
  for (auto* product : { &offscreen_preview_, &offscreen_capture_ }) {
    product->framebuffer.reset();
  }

  Stage().Clear();
  active_scene_ = {};
  scene_bootstrapper_.BindToScene(observer_ptr<scene::Scene> { nullptr });
  Base::OnShutdown();
}

auto MainModule::ResolveRenderExtent() const -> platform::window::ExtentT
{
  if (Window()) {
    if (auto framebuffer = Window()->GetCurrentFrameBuffer().lock()) {
      const auto framebuffer_extent = GetFramebufferExtent(*framebuffer);
      if (HasPositiveExtent(framebuffer_extent)) {
        return framebuffer_extent;
      }
    }
  }

  if (Window() && Window()->GetWindow()) {
    const auto window_extent = Window()->GetWindow()->Size();
    if (HasPositiveExtent(window_extent)) {
      return window_extent;
    }
  }

  return last_viewport_;
}

auto MainModule::UpdateCameras(const platform::window::ExtentT& extent) -> void
{
  if (!HasPositiveExtent(extent)) {
    DLOG_F(2, "[MultiView] Skip camera update for invalid extent {}x{}",
      extent.width, extent.height);
    return;
  }

  // Update Main camera (match legacy)
  if (main_camera_node_.IsAlive()) {
    const auto cam_opt
      = main_camera_node_.GetCameraAs<scene::PerspectiveCamera>();
    if (cam_opt) {
      auto& cam = cam_opt->get();
      constexpr auto kMainCamPos = glm::vec3(0.0F, 0.0F, 5.0F);
      constexpr float kMainCamFov = 45.0F;
      constexpr float kMainCamNear = 0.1F;
      constexpr float kMainCamFar = 100.0F;

      main_camera_node_.GetTransform().SetLocalPosition(kMainCamPos);
      // Look straight down, with +Y at the top of the image.
      main_camera_node_.GetTransform().SetLocalRotation(
        transforms::LookRotation(
          { .forward = space::move::Down, .up = space::move::Back }));
      if (config_.exposure_proof != ExposureProofScenario::kNone) {
        const bool daylight
          = config_.exposure_proof == ExposureProofScenario::kConsumerVisual;
        const glm::vec3 position = daylight ? glm::vec3(4.0F, -7.0F, 1.7F)
                                            : glm::vec3(4.0F, -6.0F, 4.0F);
        const glm::vec3 target(-0.75F, 0.0F, daylight ? 0.5F : 0.0F);
        main_camera_node_.GetTransform().SetLocalPosition(position);
        main_camera_node_.GetTransform().SetLocalRotation(
          transforms::LookRotation({ .forward = target - position }));
      }
      cam.SetFieldOfView(glm::radians(kMainCamFov));
      cam.SetAspectRatio(extent.height > 0
          ? (static_cast<float>(extent.width)
              / static_cast<float>(extent.height))
          : 1.0F);
      cam.SetNearPlane(kMainCamNear);
      cam.SetFarPlane(
        config_.exposure_proof == ExposureProofScenario::kConsumerVisual
          ? 1000.0F
          : kMainCamFar);
      cam.SetViewport(ViewPort {
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      });
    }
  }

  // Update PiP camera
  if (pip_camera_node_.IsAlive()) {
    const auto cam_opt
      = pip_camera_node_.GetCameraAs<scene::PerspectiveCamera>();
    if (cam_opt) {
      auto& cam = cam_opt->get();

      // Frame the sample objects around their shared center.
      const bool daylight
        = config_.exposure_proof == ExposureProofScenario::kConsumerVisual;
      auto pip_position = glm::vec3(-5.0F, 0.4F, 4.0F);
      if (daylight) {
        pip_position = glm::vec3(-5.0F, -4.0F, 1.5F);
      } else if (config_.exposure_proof == ExposureProofScenario::kAtmosphere) {
        pip_position = glm::vec3(-5.0F, -6.0F, 4.0F);
      }
      pip_camera_node_.GetTransform().SetLocalPosition(pip_position);

      const glm::vec3 target(-0.75F, 0.0F, daylight ? 0.5F : 0.0F);
      pip_camera_node_.GetTransform().SetLocalRotation(
        transforms::LookRotation({ .forward = target - pip_position }));

      // PiP aspect ratio from its intended viewport
      const auto pip_layout = ComputePipLayout(extent, viewport_proof_resized_);
      constexpr float kPipCamFov = 45.0F;
      constexpr float kPipCamNear = 0.05F;
      constexpr float kPipCamFar = 100.0F;

      cam.SetFieldOfView(glm::radians(kPipCamFov));
      cam.SetAspectRatio(
        pip_layout.height > 0 ? (pip_layout.width / pip_layout.height) : 1.0F);
      cam.SetNearPlane(kPipCamNear);
      cam.SetFarPlane(daylight ? 1000.0F : kPipCamFar);
      cam.SetViewport(ViewPort {
        // The camera renders into a dedicated PiP-sized offscreen target.
        // Its local viewport must start at (0,0); the absolute on-screen
        // inset belongs to CompositionView, not the camera.
        .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = pip_layout.width,
        .height = pip_layout.height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      });
    }
  }

  if (!config_.proof_layout && !config_.aux_proof_layout
    && !config_.offscreen_proof_layout
    && !config_.feature_variant_proof_layout) {
    return;
  }

  const float proof_cell_aspect = extent.height > 0
    ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
    : 1.0F;
  struct CameraPose {
    glm::vec3 position;
    glm::vec3 target;
    float fov_degrees;
  };
  const auto configure_camera
    = [](scene::SceneNode& node, const CameraPose& pose,
        const float aspect_ratio) -> void {
    if (!node.IsAlive()) {
      return;
    }
    const auto cam_opt = node.GetCameraAs<scene::PerspectiveCamera>();
    if (!cam_opt) {
      return;
    }

    auto& cam = cam_opt->get();
    node.GetTransform().SetLocalPosition(pose.position);
    node.GetTransform().SetLocalRotation(
      transforms::LookRotation({ .forward = pose.target - pose.position }));
    cam.SetFieldOfView(glm::radians(pose.fov_degrees));
    cam.SetAspectRatio(aspect_ratio);
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(160.0F);
  };

  if (config_.feature_variant_proof_layout) {
    constexpr float kMargin = 16.0F;
    constexpr float kGap = 12.0F;
    const float cell_w = std::max(1.0F,
      std::floor(
        (static_cast<float>(extent.width) - (2.0F * kMargin) - (2.0F * kGap))
        / 3.0F));
    const float cell_h = std::max(1.0F,
      std::floor(
        (static_cast<float>(extent.height) - (2.0F * kMargin) - kGap) / 2.0F));
    const float cell_aspect = cell_w / cell_h;
    configure_camera(main_camera_node_,
      CameraPose {
        .position = glm::vec3(0.0F, 0.0F, 5.0F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 42.0F,
      },
      cell_aspect);
    configure_camera(pip_camera_node_,
      CameraPose {
        .position = glm::vec3(-4.8F, 1.2F, 4.6F),
        .target = glm::vec3(-0.4F, -0.1F, -1.4F),
        .fov_degrees = 38.0F,
      },
      cell_aspect);
    configure_camera(top_camera_node_,
      CameraPose {
        .position = glm::vec3(0.0F, 8.0F, 0.2F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 45.0F,
      },
      cell_aspect);
    configure_camera(debug_camera_node_,
      CameraPose {
        .position = glm::vec3(-5.0F, 2.5F, 5.5F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 42.0F,
      },
      cell_aspect);
    configure_camera(shadow_camera_node_,
      CameraPose {
        .position = glm::vec3(5.0F, 3.5F, 4.5F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 42.0F,
      },
      cell_aspect);
    configure_camera(diagnostics_camera_node_,
      CameraPose {
        .position = glm::vec3(1.8F, 2.0F, 4.0F),
        .target = glm::vec3(-0.5F, 0.0F, -1.6F),
        .fov_degrees = 40.0F,
      },
      cell_aspect);
  } else {
    configure_camera(top_camera_node_,
      CameraPose {
        .position = glm::vec3(0.0F, 8.0F, 0.1F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 45.0F,
      },
      proof_cell_aspect);
    configure_camera(debug_camera_node_,
      CameraPose {
        .position = glm::vec3(-5.0F, 2.5F, 5.5F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 42.0F,
      },
      proof_cell_aspect);
    configure_camera(shadow_camera_node_,
      CameraPose {
        .position = glm::vec3(5.0F, 3.5F, 4.5F),
        .target = glm::vec3(0.0F, 0.0F, -2.0F),
        .fov_degrees = 42.0F,
      },
      proof_cell_aspect);
  }

  struct TargetSize {
    uint32_t width;
    uint32_t height;
  };
  const auto configure_offscreen_camera
    = [](scene::SceneNode& node, const CameraPose& pose,
        const TargetSize size) -> void {
    if (!node.IsAlive()) {
      return;
    }
    const auto cam_opt = node.GetCameraAs<scene::PerspectiveCamera>();
    if (!cam_opt) {
      return;
    }

    auto& cam = cam_opt->get();
    node.GetTransform().SetLocalPosition(pose.position);
    node.GetTransform().SetLocalRotation(
      transforms::LookRotation({ .forward = pose.target - pose.position }));
    cam.SetFieldOfView(glm::radians(pose.fov_degrees));
    cam.SetAspectRatio(
      static_cast<float>(size.width) / static_cast<float>(size.height));
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(160.0F);
    cam.SetViewport(MakeLocalView(size.width, size.height).viewport);
  };

  configure_offscreen_camera(offscreen_preview_camera_node_,
    CameraPose {
      .position = glm::vec3(-3.2F, 2.2F, 4.8F),
      .target = glm::vec3(-0.75F, 0.0F, 0.0F),
      .fov_degrees = 45.0F,
    },
    TargetSize {
      .width = kOffscreenPreviewWidth,
      .height = kOffscreenPreviewHeight,
    });
  configure_offscreen_camera(offscreen_capture_camera_node_,
    CameraPose {
      .position = glm::vec3(5.0F, 4.0F, 4.0F),
      .target = glm::vec3(-0.75F, 0.0F, 0.0F),
      .fov_degrees = 45.0F,
    },
    TargetSize {
      .width = kOffscreenCaptureWidth,
      .height = kOffscreenCaptureHeight,
    });
}

auto MainModule::OnFrameStart(
  observer_ptr<oxygen::engine::FrameContext> context) -> void
{
  DCHECK_NOTNULL_F(context);
  Base::OnFrameStart(context);
  if (!HasRenderableWindow() || !Stage().TryGetScene()) {
    return;
  }

  // Ensure content exists
  (void)scene_bootstrapper_.EnsureSceneWithContent();

  if (config_.exposure_proof == ExposureProofScenario::kConsumerVisual
    && config_.visual_fog_cycle) {
    const auto frame = context->GetFrameSequenceNumber().get();
    if (frame == 32U) {
      pending_visual_fog_mode_ = VisualFogMode::kVolumetric;
    }
    if (frame == 96U) {
      pending_visual_fog_mode_ = VisualFogMode::kLocal;
    }
    if (frame == 128U && !config_.visual_fog_hold_local) {
      pending_visual_fog_mode_ = VisualFogMode::kClear;
    }
  }
  if (pending_visual_fog_mode_) {
    config_.visual_fog_mode = *pending_visual_fog_mode_;
    pending_visual_fog_mode_.reset();
    scene_bootstrapper_.ApplyConsumerVisualProof(config_.visual_fog_mode);
  }

  if (config_.exposure_proof == ExposureProofScenario::kAtmosphere) {
    scene_bootstrapper_.ApplyAtmosphereProof(
      context->GetFrameSequenceNumber().get());
  }
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  auto published_camera = Stage().TakePublishedMainCamera();
  if (published_camera.IsAlive()) {
    main_camera_node_ = std::move(published_camera);
  }
  scene_bootstrapper_.BindToScene(Stage().TryGetScene());
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> context) -> void
{
  Base::OnFrameEnd(context);
  if ((config_.exposure_proof == ExposureProofScenario::kWindowResize
        || config_.exposure_proof == ExposureProofScenario::kInteractions)
    && Window() && Window()->GetWindow()) {
    const auto frame = context->GetFrameSequenceNumber().get();
    const bool interactions
      = config_.exposure_proof == ExposureProofScenario::kInteractions;
    if (frame == (interactions ? 88U : 44U)) {
      window_proof_initial_extent_ = Window()->GetWindow()->Size();
      Window()->GetWindow()->Resize({ .width = 1280U, .height = 800U });
    } else if (frame == (interactions ? 96U : 52U)) {
      Window()->GetWindow()->Resize(window_proof_initial_extent_);
    }
  }
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  // The visual check shows only its own window over the views.
  if (config_.exposure_proof != ExposureProofScenario::kConsumerVisual) {
    co_await Base::OnGuiUpdate(context);
  } else {
    static_cast<void>(co_await DrawHostUi(context));
  }
  if (!HasRenderableWindow() || !active_scene_.IsValid()
    || !main_camera_node_.IsAlive()) {
    co_return;
  }
  DrawFeatureVariantProofOverlay();
  DrawConsumerVisualControls();
  DrawInteractionProofOverlay(context->GetFrameSequenceNumber().get());

  co_return;
}

auto MainModule::DrawConsumerVisualControls() -> void
{
  if (config_.exposure_proof != ExposureProofScenario::kConsumerVisual) {
    return;
  }
  const auto imgui_module = ResolveImGuiModule();
  if (!imgui_module || !imgui_module->IsFrameActive()) {
    return;
  }
  auto* imgui_context = imgui_module->GetImGuiContext();
  if (!imgui_context) {
    return;
  }
  ImGui::SetCurrentContext(imgui_context);
  ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.94F);
  constexpr auto flags = static_cast<ImGuiWindowFlags>(
    static_cast<unsigned>(ImGuiWindowFlags_AlwaysAutoResize)
    | static_cast<unsigned>(ImGuiWindowFlags_NoCollapse)
    | static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings));
  if (ImGui::Begin("EX05-15 visual check", nullptr, flags)) {
    ImGui::TextUnformatted(
      "Original materials | Fixed cameras | Exposure: EV15");
    ImGui::TextUnformatted("Current scene format: FP32 in both views");
    ImGui::Separator();
    auto mode = static_cast<int>(config_.visual_fog_mode);
    bool changed = ImGui::RadioButton(
      "Clear (reference)", &mode, static_cast<int>(VisualFogMode::kClear));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
      "Fog", &mode, static_cast<int>(VisualFogMode::kVolumetric));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
      "Local fog", &mode, static_cast<int>(VisualFogMode::kLocal));
    if (changed) {
      constexpr auto kNames = std::array { "clear", "volumetric", "local" };
      GetShell().GetPanelCommands()->RunText(
        "multiview.fog", kNames.at(static_cast<std::size_t>(mode)));
    }
    ImGui::TextUnformatted(
      "Compare material colors, smooth haze and object edges.");
    ImGui::TextUnformatted("PiP shows the same scene from another angle.");
  }
  ImGui::End();
}

auto MainModule::EnsureOffscreenProofProduct(OffscreenProofProduct& product,
  const uint32_t width, const uint32_t height, std::string_view debug_name)
  -> bool
{
  auto gfx = Host().gfx_weak.lock();
  if (!gfx) {
    return false;
  }

  if (product.framebuffer && product.width == width
    && product.height == height) {
    return true;
  }

  product.framebuffer.reset();
  product.width = width;
  product.height = height;

  auto color_desc = graphics::TextureDesc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.02F, 0.02F, 0.025F, 1.0F };
  color_desc.debug_name = std::string(debug_name);

  auto color = gfx->CreateTexture(color_desc);
  if (!color) {
    return false;
  }

  auto fb_desc = graphics::FramebufferDesc {};
  fb_desc.AddColorAttachment({ .texture = std::move(color) });
  product.framebuffer = gfx->CreateFramebuffer(fb_desc);
  return static_cast<bool>(product.framebuffer);
}

auto MainModule::RenderOffscreenProofProducts(engine::FrameContext& context)
  -> void
{
  auto renderer = ResolveRenderer();
  if (!renderer) {
    return;
  }
  if (!active_scene_.IsValid() || !offscreen_preview_camera_node_.IsAlive()
    || !offscreen_capture_camera_node_.IsAlive()) {
    return;
  }

  const bool preview_ready
    = EnsureOffscreenProofProduct(offscreen_preview_, kOffscreenPreviewWidth,
      kOffscreenPreviewHeight, "M06B.OffscreenPreview.Deferred.Color");
  const bool capture_ready
    = EnsureOffscreenProofProduct(offscreen_capture_, kOffscreenCaptureWidth,
      kOffscreenCaptureHeight, "M06B.OffscreenCapture.Forward.Color");
  if (!preview_ready || !capture_ready) {
    LOG_F(WARNING, "[MultiView] M06B offscreen proof targets are not ready");
    return;
  }

  const auto delta_seconds
    = std::chrono::duration<float>(context.GetGameDeltaTime().get()).count();
  const auto frame_session = vortex::Renderer::FrameSessionInput {
    .frame_slot = context.GetFrameSlot(),
    .frame_sequence = context.GetFrameSequenceNumber(),
    .delta_time_seconds
    = std::max(delta_seconds, time::SimulationClock::kMinDeltaTimeSeconds),
    .scene = observer_ptr<scene::Scene> { active_scene_.operator->() },
  };

  auto render_product
    = [&](OffscreenProofProduct& product, const char* name,
        const ViewId view_id, const scene::SceneNode& camera,
        const vortex::Renderer::OffscreenPipelineInput pipeline,
        const bool force_wireframe) -> void {
    if (!config_.exposure_view_only.empty()
      && config_.exposure_view_only != name) {
      return;
    }
    if (IsLayoutExposureProof(config_.exposure_proof)
      && context.GetFrameSequenceNumber().get() == 32U) {
      const auto request = renderer->QueueExposureTransition(
        vortex::CompositionView::ViewStateHandle { view_id.get() },
        vortex::ExposureTransitionPolicy::kRemeter);
      CHECK_F(request.has_value(), "Offscreen layout proof remeter failed");
    }
    auto facade = renderer->ForOffscreenScene();
    facade.SetFrameSession(frame_session);
    facade.SetSceneSource(vortex::Renderer::SceneSourceInput {
      .scene = observer_ptr<scene::Scene> { active_scene_.operator->() },
    });
    auto view_intent
      = vortex::Renderer::OffscreenSceneViewInput::FromCamera(
        name, view_id, MakeLocalView(product.width, product.height), camera)
          .SetViewStateHandle(
            vortex::CompositionView::ViewStateHandle { view_id.get() })
          .SetWithAtmosphere(true)
          .SetForceWireframe(force_wireframe)
          .SetClearColor(graphics::Color { 0.025F, 0.035F, 0.05F, 1.0F });
    if (IsLayoutExposureProof(config_.exposure_proof)) {
      auto exposure = scene::ExposureSettings {};
      exposure.key = 12.5F;
      // Preserve the scene-authored profile for these proof cameras. The
      // preview's central shadow is not representative of its lit image.
      exposure.metering_mode = engine::MeteringMode::kAverage;
      view_intent.SetExposureOverride(std::move(exposure));
    }
    facade.SetViewIntent(view_intent);
    facade.SetOutputTarget(vortex::Renderer::OutputTargetInput {
      .framebuffer
      = observer_ptr<graphics::Framebuffer> { product.framebuffer.get() },
    });
    facade.SetPipeline(pipeline);

    const auto report = facade.Validate();
    if (!report.Ok()) {
      for (const auto& issue : report.issues) {
        LOG_F(WARNING, "[MultiView] M06B offscreen proof {} validation {}: {}",
          name, issue.code, issue.message);
      }
      return;
    }

    auto session = facade.Finalize();
    if (!session.has_value()) {
      LOG_F(
        WARNING, "[MultiView] M06B offscreen proof {} finalize failed", name);
      return;
    }

    if (!session->ExecuteInsideFrame(context)) {
      LOG_F(
        WARNING, "[MultiView] Offscreen proof {} did not produce output", name);
      return;
    }
    LOG_F(INFO, "Vortex.OffscreenProof.Render name={} view_id={} texture={}",
      name, view_id.get(),
      ResolveFramebufferColorTexture(*product.framebuffer)
        ->GetDescriptor()
        .debug_name);
  };

  render_product(offscreen_preview_, kOffscreenPreviewName.data(),
    kOffscreenPreviewViewId, offscreen_preview_camera_node_,
    vortex::Renderer::OffscreenPipelineInput::Deferred(), false);
  render_product(offscreen_capture_, kOffscreenCaptureName.data(),
    kOffscreenCaptureViewId, offscreen_capture_camera_node_,
    vortex::Renderer::OffscreenPipelineInput::Forward(), false);
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  if (!HasRenderableWindow()) {
    co_return;
  }
  if (!active_scene_.IsValid() && !Stage().HasStagedScene()) {
    StageScene();
  }

  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  const auto extent = ResolveRenderExtent();
  if (HasPositiveExtent(extent)
    && (extent.width != last_viewport_.width
      || extent.height != last_viewport_.height)) {
    UpdateCameras(extent);
    last_viewport_ = extent;
  }

  co_await Base::OnSceneMutation(context);
  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> oxygen::co::Co<>
{
  if (config_.offscreen_proof_layout) {
    RenderOffscreenProofProducts(*context);
  }

  co_return;
}

auto MainModule::AppendCompositionLayers(engine::FrameContext& /*context*/,
  vortex::Renderer::RuntimeCompositionInput& input) -> void
{
  if (!config_.offscreen_proof_layout || !offscreen_preview_.framebuffer
    || !offscreen_capture_.framebuffer) {
    return;
  }

  const auto preview_texture
    = ResolveFramebufferColorTexture(*offscreen_preview_.framebuffer);
  const auto capture_texture
    = ResolveFramebufferColorTexture(*offscreen_capture_.framebuffer);
  if (!preview_texture || !capture_texture) {
    return;
  }

  const auto extent = ResolveRenderExtent();
  if (!HasPositiveExtent(extent)) {
    return;
  }

  constexpr float kMargin = 28.0F;
  constexpr float kGap = 18.0F;
  const auto sw = static_cast<float>(extent.width);
  const auto sh = static_cast<float>(extent.height);
  const float preview_w = std::min(512.0F, std::floor(sw * 0.30F));
  const float preview_h = std::floor(preview_w * 9.0F / 16.0F);
  const float capture_size = std::min(256.0F, std::floor(sh * 0.22F));
  const float panel_y = std::max(kMargin, sh - kMargin - preview_h);

  if (config_.exposure_view_only.empty()
    || config_.exposure_view_only == kOffscreenPreviewName) {
    input.texture_layers.push_back(vortex::Renderer::RuntimeTextureCompositionLayer {
    .source_texture = preview_texture,
    .viewport = ViewPort {
      .top_left_x = kMargin,
      .top_left_y = panel_y,
      .width = preview_w,
      .height = preview_h,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    },
    .opacity = 1.0F,
    .debug_name = "M06B.OffscreenPreview.Deferred.Composite",
  });
  }

  if (config_.exposure_view_only.empty()
    || config_.exposure_view_only == kOffscreenCaptureName) {
    input.texture_layers.push_back(vortex::Renderer::RuntimeTextureCompositionLayer {
    .source_texture = capture_texture,
    .viewport = ViewPort {
      .top_left_x = kMargin + preview_w + kGap,
      .top_left_y = std::max(kMargin, sh - kMargin - capture_size),
      .width = capture_size,
      .height = capture_size,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    },
    .opacity = 1.0F,
    .debug_name = "M06B.OffscreenCapture.Forward.Composite",
  });
  }
}

auto MainModule::DrawFeatureVariantProofOverlay() -> void
{
  if (!config_.feature_variant_proof_layout) {
    return;
  }

  const auto extent = ResolveRenderExtent();
  if (!HasPositiveExtent(extent)) {
    return;
  }

  constexpr float kMargin = 16.0F;
  constexpr float kGap = 12.0F;
  constexpr float kPadX = 10.0F;
  constexpr float kPadY = 6.0F;
  constexpr float kHeaderHeight = 30.0F;
  const auto sw = static_cast<float>(extent.width);
  const auto sh = static_cast<float>(extent.height);
  const float cell_w = std::max(
    1.0F, std::floor((sw - (2.0F * kMargin) - (2.0F * kGap)) / 3.0F));
  const float cell_h
    = std::max(1.0F, std::floor((sh - (2.0F * kMargin) - kGap) / 2.0F));

  struct Label {
    const char* text;
    ImU32 color;
  };
  const std::array labels {
    Label {
      .text = "M06C Depth-only | BLACK expected",
      .color = IM_COL32(76, 120, 190, 235),
    },
    Label {
      .text = "M06C Shadow-only | BLACK expected",
      .color = IM_COL32(90, 80, 62, 235),
    },
    Label {
      .text = "M06C No environment | no sky/fog",
      .color = IM_COL32(150, 82, 80, 235),
    },
    Label {
      .text = "M06C No shadowing | no shadows",
      .color = IM_COL32(70, 125, 92, 235),
    },
    Label {
      .text = "M06C No volumetrics | no volume fog",
      .color = IM_COL32(115, 88, 150, 235),
    },
    Label {
      .text = "M06C Diagnostics-only | BLACK expected",
      .color = IM_COL32(95, 95, 95, 235),
    },
  };

  auto* draw_list = ImGui::GetForegroundDrawList();
  if (draw_list == nullptr) {
    return;
  }

  for (std::size_t index = 0U; index < labels.size(); ++index) {
    const auto column = index % 3U;
    const auto row = index / 3U;
    const float x = kMargin + (static_cast<float>(column) * (cell_w + kGap));
    const float y = kMargin + (static_cast<float>(row) * (cell_h + kGap));
    const ImVec2 rect_min { x, y };
    const ImVec2 rect_max { x + cell_w, y + kHeaderHeight };
    draw_list->AddRectFilled(rect_min, rect_max, labels[index].color, 0.0F);
    const ImVec4 clip_rect { x + kPadX, y, x + cell_w - kPadX,
      y + kHeaderHeight };
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
      ImVec2 { x + kPadX, y + kPadY }, IM_COL32(255, 255, 255, 255),
      labels[index].text, nullptr, 0.0F, &clip_rect);
    draw_list->AddRect(ImVec2 { x, y }, ImVec2 { x + cell_w, y + cell_h },
      IM_COL32(255, 255, 255, 180), 0.0F, 0, 1.5F);
  }
}

auto MainModule::BuildViews(
  oxygen::engine::FrameContext& context, host::HostViews& host_views) -> void
{
  auto& views = host_views.views;
  main_view_.reset();
  const auto sequence = context.GetFrameSequenceNumber().get();
  const bool interactions
    = config_.exposure_proof == ExposureProofScenario::kInteractions;
  retain_inactive_pip_
    = (config_.exposure_proof == ExposureProofScenario::kLifetime
        && sequence >= 44U && sequence < 48U)
    || (interactions && sequence >= 100U && sequence < 104U);
  if (interactions && sequence == 108U) {
    pip_view_id_ = Publisher().GetOrCreateViewId("InteractionsRecreatedPiP");
  }
  if (config_.exposure_proof == ExposureProofScenario::kLifetime
    && sequence == 52U) {
    pip_view_id_ = Publisher().GetOrCreateViewId("RecreatedPipView");
  }
  viewport_proof_resized_
    = (config_.exposure_proof == ExposureProofScenario::kViewport
        && sequence >= 44U && sequence < 52U)
    || (interactions && sequence >= 76U && sequence < 84U);
  viewport_proof_scissored_
    = viewport_proof_resized_ && sequence >= (interactions ? 80U : 48U);
  BuildComposition(context, views);
  host_views.main = main_view_;
  for (auto& view : views) {
    if (view.camera.has_value()) {
      // The demo owns these logical IDs for the complete view lifetime.
      view.view_state_handle
        = vortex::CompositionView::ViewStateHandle { view.id.get() };
      if (config_.proof_wireframe_overlay) {
        view.render_settings.render_mode
          = vortex::RenderMode::kOverlayWireframe;
      }
    }
  }
  if (config_.proof_wireframe_overlay) {
    const std::array colors {
      graphics::Color { .8F, .2F, .2F, 1.0F },
      graphics::Color { .2F, .8F, .2F, 1.0F },
      graphics::Color { .2F, .2F, .8F, 1.0F },
    };
    if (const auto renderer = ResolveRenderer()) {
      renderer->SetWireframeColor(
        colors[context.GetFrameSequenceNumber().get() % colors.size()]);
    }
  }
  const auto proof = config_.exposure_proof;
  if (proof == ExposureProofScenario::kNone) {
    return;
  }
  // Proof inputs own the poses after DemoShell activates the selected camera.
  UpdateCameras(ResolveRenderExtent());
  const auto frame = context.GetFrameSequenceNumber().get();
  const bool source_lost
    = (proof == ExposureProofScenario::kSourceLoss && frame >= 44U)
    || (interactions && frame >= 120U && frame < 128U);
  if (proof == ExposureProofScenario::kSourceLoss) {
    if (frame == 46U) {
      Host().engine->Resume();
    }
    if (frame >= 43U && frame <= 52U) {
      LOG_F(INFO, "Vortex.MultiView.SourceLoss frame={} delta_seconds={:.9g}",
        frame,
        std::chrono::duration<float>(context.GetGameDeltaTime().get()).count());
    }
  }
  auto renderer = ResolveRenderer();
  CHECK_NOTNULL_F(renderer.get());
  if (proof == ExposureProofScenario::kConsumerVisual
    && renderer->GetGroundGridConfig().enabled) {
    auto grid = renderer->GetGroundGridConfig();
    grid.enabled = false;
    renderer->SetGroundGridConfig(grid);
  }
  for (auto& view : views) {
    if (!view.camera.has_value()) {
      continue;
    }
    auto exposure = scene::ExposureSettings {};
    exposure.key = 12.5F;
    exposure.compensation_ev = view.id == pip_view_id_ ? 2.0F : 0.0F;
    if (proof == ExposureProofScenario::kAtmosphere) {
      exposure.mode = engine::ExposureMode::kManual;
      exposure.manual_ev = 2.0F;
      exposure.compensation_ev = 0.0F;
      view.with_atmosphere = true;
      view.with_height_fog = false;
      view.shading_mode = frame >= 56U ? vortex::ShadingMode::kForward
                                       : vortex::ShadingMode::kDeferred;
    } else if (proof == ExposureProofScenario::kConsumerVisual) {
      exposure.mode = engine::ExposureMode::kManual;
      exposure.manual_ev = 15.0F;
      exposure.compensation_ev = 0.0F;
      view.with_atmosphere = true;
      view.with_height_fog = config_.visual_fog_mode != VisualFogMode::kClear;
      view.with_local_fog = config_.visual_fog_mode == VisualFogMode::kLocal;
      view.shading_mode = vortex::ShadingMode::kDeferred;
    } else if (proof == ExposureProofScenario::kAtmosphereLit) {
      exposure.compensation_ev = 0.0F;
      exposure.metering_mode = frame < 48U ? engine::MeteringMode::kAverage
                                           : engine::MeteringMode::kSpot;
      view.with_atmosphere = true;
      view.with_height_fog = false;
      view.shading_mode = vortex::ShadingMode::kDeferred;
    }
    if (IsLayoutExposureProof(proof)) {
      exposure.compensation_ev = 0;
      // The NoEnvironment camera's center lies in a deep cast shadow. Meter
      // its whole image so the proof retains readable directly-lit materials.
      exposure.metering_mode = IsMixedProof(proof)
          || view.feature_profile
            == vortex::CompositionView::ViewFeatureProfile::kNoEnvironment
        ? engine::MeteringMode::kAverage
        : engine::MeteringMode::kSpot;
      // Exercise an exposed lit auxiliary product instead of normals colors.
      if (config_.aux_proof_layout && view.id == debug_view_id_) {
        view.render_settings.shader_debug_mode
          = vortex::ShaderDebugMode::kDisabled;
      }
    } else {
      view.force_wireframe = false;
    }
    if (proof == ExposureProofScenario::kModes && view.id == pip_view_id_) {
      if (frame >= 44U && frame < 48U) {
        view.force_wireframe = true;
      } else if (frame >= 52U && frame < 56U) {
        exposure.mode = engine::ExposureMode::kManual;
        exposure.manual_ev = 4.0F;
      } else if (frame >= 56U && frame < 60U) {
        exposure.enabled = false;
      } else if (frame >= 60U && frame < 64U) {
        exposure.mode = engine::ExposureMode::kManualCamera;
        auto camera = pip_camera_node_.GetCameraAs<scene::PerspectiveCamera>();
        CHECK_F(camera.has_value());
        camera->get().SetExposure(
          { .aperture_f = 11.0F, .shutter_rate = 125.0F, .iso = 100.0F });
      } else if (frame >= 64U && frame < 68U) {
        exposure.target_luminance = 0.0F;
      }
    }
    if (proof == ExposureProofScenario::kLifetime && frame >= 48U
      && view.id == pip_view_id_) {
      exposure.compensation_ev = 3.0F;
    }
    if (proof == ExposureProofScenario::kShared
      || (proof == ExposureProofScenario::kSourceLoss && !source_lost)) {
      if (view.id == pip_view_id_) {
        view.exposure_source_view_id = main_view_id_;
      } else if (proof == ExposureProofScenario::kShared && frame >= 44U) {
        exposure.compensation_ev = 1.0F;
      }
    }
    view.render_settings.exposure = std::move(exposure);
  }
  if (interactions) {
    ApplyInteractionProof(context, views);
  }
  std::erase_if(views, [&](const auto& view) -> auto {
    return view.camera.has_value()
      && ((retain_inactive_pip_ && view.id == pip_view_id_)
        || (proof == ExposureProofScenario::kMainOnly
          && view.id != main_view_id_)
        || ((proof == ExposureProofScenario::kPipOnly || source_lost)
          && view.id != pip_view_id_));
  });
  for (const auto& view : views) {
    if (!view.camera.has_value()
      || view.exposure_source_view_id != kInvalidViewId) {
      continue;
    }
    if ((frame == 32U && proof != ExposureProofScenario::kAtmosphere
          && proof != ExposureProofScenario::kConsumerVisual)
      || (proof == ExposureProofScenario::kShared && frame == 44U)
      || (proof == ExposureProofScenario::kAtmosphereLit && frame == 48U)) {
      const auto request = renderer->QueueExposureTransition(
        view.view_state_handle, vortex::ExposureTransitionPolicy::kRemeter);
      CHECK_F(request.has_value(), "MultiView proof remeter request failed");
      LOG_F(INFO, "Vortex.MultiView.ExposureProof frame={} remeter_view={}",
        frame, view.id.get());
    }
    if (proof == ExposureProofScenario::kModes && view.id == pip_view_id_) {
      if (frame == 72U) {
        const auto request
          = renderer->QueueExposureTransition(view.view_state_handle,
            vortex::ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
        CHECK_F(request.has_value());
      } else if (frame == 76U) {
        const auto request = renderer->NotifyViewDiscontinuity(
          view.view_state_handle, vortex::ViewDiscontinuity::kCameraCut);
        CHECK_F(request.has_value());
      }
    }
  }
  if (IsLayoutExposureProof(proof) && !config_.exposure_view_only.empty()) {
    const auto selected = std::ranges::find(
      views, config_.exposure_view_only, &vortex::CompositionView::name);
    const bool offscreen = config_.offscreen_proof_layout
      && (config_.exposure_view_only == kOffscreenPreviewName
        || config_.exposure_view_only == kOffscreenCaptureName);
    CHECK_F(selected != views.end() || offscreen,
      "Exposure proof view '{}' is not in this layout",
      config_.exposure_view_only);
    std::vector<ViewId> needed;
    if (selected != views.end()) {
      needed.push_back(selected->id);
    }
    for (size_t index = 0; index < needed.size(); ++index) {
      const auto consumer
        = std::ranges::find(views, needed[index], &vortex::CompositionView::id);
      CHECK_F(consumer != views.end());
      for (const auto& input : consumer->consumed_aux_outputs) {
        const auto producer
          = std::ranges::find_if(views, [&](const auto& view) -> auto {
              return std::ranges::any_of(
                view.produced_aux_outputs, [&](const auto& output) -> auto {
                  return output.id == input.id;
                });
            });
        if (producer != views.end()
          && std::ranges::find(needed, producer->id) == needed.end()) {
          needed.push_back(producer->id);
        }
      }
    }
    std::erase_if(views, [&](const auto& view) -> auto {
      return view.camera.has_value()
        && std::ranges::find(needed, view.id) == needed.end();
    });
  }
  if (proof == ExposureProofScenario::kReordered) {
    const auto main
      = std::ranges::find(views, main_view_id_, &vortex::CompositionView::id);
    const auto pip
      = std::ranges::find(views, pip_view_id_, &vortex::CompositionView::id);
    if (main != views.end() && pip != views.end()) {
      std::iter_swap(main, pip);
    }
  }
}

auto MainModule::ApplyInteractionProof(engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
{
  const auto frame = context.GetFrameSequenceNumber().get();
  if (frame == 40U) {
    Host().engine->Resume();
  }
  if (frame == 136U) {
    Host().engine->Pause();
  }
  const auto renderer = ResolveRenderer();
  CHECK_NOTNULL_F(renderer.get());
  if (frame >= 40U) {
    const float phase
      = static_cast<float>(std::min(frame, uint64_t { 132 }) - 40U);
    const auto move_camera
      = [&](scene::SceneNode& node, float direction) -> void {
      if (!node.IsAlive()) {
        return;
      }
      const auto base = node.GetTransform().GetLocalPosition();
      CHECK_F(base.has_value());
      const auto position = *base
        + glm::vec3 { direction * .6F * std::sin(phase * .07F),
            .3F * std::sin(phase * .045F), 0 };
      node.GetTransform().SetLocalPosition(position);
      node.GetTransform().SetLocalRotation(transforms::LookRotation(
        { .forward = glm::vec3 { -.75F, 0, 0 } - position }));
    };
    move_camera(main_camera_node_, 1);
    move_camera(pip_camera_node_, -1);
    move_camera(offscreen_preview_camera_node_, 1);
    move_camera(offscreen_capture_camera_node_, -1);
  }
  for (auto& view : views) {
    if (!view.camera.has_value()
      || !view.render_settings.exposure.has_value()) {
      continue;
    }
    auto exposure = *view.render_settings.exposure;
    if (view.id == pip_view_id_) {
      exposure.compensation_ev = frame >= 44U && frame < 52U ? .5F : 0;
      if (frame >= 52U && frame < 60U) {
        exposure.mode = engine::ExposureMode::kManual;
        exposure.manual_ev = 14.5F;
      }
      view.shading_mode = frame >= 56U && frame < 96U
        ? vortex::ShadingMode::kForward
        : vortex::ShadingMode::kDeferred;
      if (frame >= 112U && frame < 120U) {
        view.exposure_source_view_id = main_view_id_;
      }
      if (frame == 64U) {
        CHECK_F(renderer
            ->QueueExposureTransition(view.view_state_handle,
              vortex::ExposureTransitionPolicy::kSeedFromEv100, 15.0F)
            .has_value());
      } else if (frame == 68U) {
        CHECK_F(renderer
            ->NotifyViewDiscontinuity(
              view.view_state_handle, vortex::ViewDiscontinuity::kCameraCut)
            .has_value());
      }
    } else if (view.id == main_view_id_ && frame == 116U) {
      CHECK_F(renderer
          ->QueueExposureTransition(view.view_state_handle,
            vortex::ExposureTransitionPolicy::kSeedFromEv100, 15.0F)
          .has_value());
    }
    view.render_settings.exposure = std::move(exposure);
  }
  if (frame >= 72U && frame < 80U) {
    std::ranges::reverse(views);
  }
  LOG_F(INFO,
    "Vortex.MultiView.Interactions frame={} delta_seconds={:.9g} main={} "
    "pip={} "
    "hidden={} resized={} shared={} source_removed={}",
    frame,
    std::chrono::duration<double>(context.GetGameDeltaTime().get()).count(),
    main_view_id_.get(), pip_view_id_.get(), retain_inactive_pip_,
    viewport_proof_resized_, frame >= 112U && frame < 120U,
    frame >= 120U && frame < 128U);
}

auto MainModule::DrawInteractionProofOverlay(std::uint64_t frame) -> void
{
  if (!IsMixedProof(config_.exposure_proof)) {
    return;
  }
  const auto imgui_module = ResolveImGuiModule();
  if (!imgui_module || !imgui_module->IsFrameActive()
    || !imgui_module->GetImGuiContext()) {
    return;
  }
  ImGui::SetCurrentContext(imgui_module->GetImGuiContext());
  ImGui::SetNextWindowPos(ImVec2(128, 20), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(.9F);
  constexpr auto flags = static_cast<ImGuiWindowFlags>(
    static_cast<unsigned>(ImGuiWindowFlags_AlwaysAutoResize)
    | static_cast<unsigned>(ImGuiWindowFlags_NoCollapse)
    | static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings)
    | static_cast<unsigned>(ImGuiWindowFlags_NoInputs));
  if (ImGui::Begin("Mixed exposure validation", nullptr, flags)) {
    ImGui::TextUnformatted(
      "Sphere: opaque | Cube: emissive | Cone: masked | Cylinder: translucent");
    const auto phase = config_.exposure_proof == ExposureProofScenario::kMixed
      ? std::string_view { "Static reference" }
      : InteractionPhaseName(frame);
    const auto line = fmt::format("Frame {} | {}", frame, phase);
    ImGui::TextUnformatted(line.c_str());
  }
  ImGui::End();
}

auto MainModule::BuildComposition(oxygen::engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
{
  if (!main_camera_node_.IsAlive()) {
    return;
  }

  const auto extent = ResolveRenderExtent();
  if (!HasPositiveExtent(extent)) {
    DLOG_F(2, "[MultiView] Skip composition update for invalid extent {}x{}",
      extent.width, extent.height);
    return;
  }
  const auto sw = static_cast<float>(extent.width);
  const auto sh = static_cast<float>(extent.height);

  if (config_.feature_variant_proof_layout) {
    constexpr float kMargin = 16.0F;
    constexpr float kGap = 12.0F;
    const float cell_w = std::max(
      1.0F, std::floor((sw - (2.0F * kMargin) - (2.0F * kGap)) / 3.0F));
    const float cell_h
      = std::max(1.0F, std::floor((sh - (2.0F * kMargin) - kGap) / 2.0F));
    const auto make_view = [cell_w, cell_h](const std::size_t index) -> View {
      const auto column = index % 3U;
      const auto row = index / 3U;
      const float x = kMargin + (static_cast<float>(column) * (cell_w + kGap));
      const float y = kMargin + (static_cast<float>(row) * (cell_h + kGap));
      View view {};
      view.viewport = ViewPort {
        .top_left_x = x,
        .top_left_y = y,
        .width = cell_w,
        .height = cell_h,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      view.scissor = Scissors {
        .left = static_cast<int32_t>(x),
        .top = static_cast<int32_t>(y),
        .right = static_cast<int32_t>(x + cell_w),
        .bottom = static_cast<int32_t>(y + cell_h),
      };
      return view;
    };
    const auto configure_variant
      = [](vortex::CompositionView& view,
          const vortex::CompositionView::ViewFeatureProfile profile,
          const graphics::Color clear_color) -> void {
      const auto spec = vortex::ResolveViewFeatureProfileSpec(profile);
      view.feature_profile = profile;
      view.feature_mask = spec.feature_mask;
      view.with_atmosphere = true;
      view.with_height_fog = true;
      view.clear_color = clear_color;
    };

    auto depth_only = vortex::CompositionView::ForScene(
      main_view_id_, make_view(0U), main_camera_node_);
    depth_only.name = "M06C.DepthOnly";
    configure_variant(depth_only,
      vortex::CompositionView::ViewFeatureProfile::kDepthOnly,
      graphics::Color { 0.04F, 0.08F, 0.18F, 1.0F });
    main_view_ = depth_only.id;
    views.push_back(std::move(depth_only));

    auto shadow_only = vortex::CompositionView::ForScene(
      shadow_view_id_, make_view(1U), shadow_camera_node_);
    shadow_only.name = "M06C.ShadowOnly";
    configure_variant(shadow_only,
      vortex::CompositionView::ViewFeatureProfile::kShadowOnly,
      graphics::Color { 0.10F, 0.08F, 0.04F, 1.0F });
    views.push_back(std::move(shadow_only));

    auto no_environment = vortex::CompositionView::ForScene(
      pip_view_id_, make_view(2U), pip_camera_node_);
    no_environment.name = "M06C.NoEnvironment";
    configure_variant(no_environment,
      vortex::CompositionView::ViewFeatureProfile::kNoEnvironment,
      graphics::Color { 0.16F, 0.06F, 0.05F, 1.0F });
    views.push_back(std::move(no_environment));

    auto no_shadowing = vortex::CompositionView::ForScene(
      debug_view_id_, make_view(3U), debug_camera_node_);
    no_shadowing.name = "M06C.NoShadowing";
    configure_variant(no_shadowing,
      vortex::CompositionView::ViewFeatureProfile::kNoShadowing,
      graphics::Color { 0.04F, 0.12F, 0.08F, 1.0F });
    views.push_back(std::move(no_shadowing));

    auto no_volumetrics = vortex::CompositionView::ForScene(
      top_view_id_, make_view(4U), top_camera_node_);
    no_volumetrics.name = "M06C.NoVolumetrics";
    configure_variant(no_volumetrics,
      vortex::CompositionView::ViewFeatureProfile::kNoVolumetrics,
      graphics::Color { 0.08F, 0.06F, 0.16F, 1.0F });
    views.push_back(std::move(no_volumetrics));

    auto diagnostics_only = vortex::CompositionView::ForScene(
      diagnostics_view_id_, make_view(5U), diagnostics_camera_node_);
    diagnostics_only.name = "M06C.DiagnosticsOnly";
    configure_variant(diagnostics_only,
      vortex::CompositionView::ViewFeatureProfile::kDiagnosticsOnly,
      graphics::Color { 0.08F, 0.08F, 0.08F, 1.0F });
    views.push_back(std::move(diagnostics_only));

    for (const auto& view : views) {
      LOG_F(INFO,
        "Vortex.FeatureVariantProof.View name={} profile={} feature_mask={} "
        "viewport={}x{}+{},{}",
        view.name, vortex::ToString(view.feature_profile),
        view.feature_mask.bits.get(), view.view.viewport.width,
        view.view.viewport.height, view.view.viewport.top_left_x,
        view.view.viewport.top_left_y);
    }
    return;
  }

  if (config_.aux_proof_layout) {
    const float half_w = std::floor(sw * 0.5F);
    const float half_h = std::floor(sh * 0.5F);
    const auto make_view = [](const float x, const float y, const float width,
                             const float height) -> View {
      View view {};
      view.viewport = ViewPort {
        .top_left_x = x,
        .top_left_y = y,
        .width = width,
        .height = height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      view.scissor = Scissors {
        .left = static_cast<int32_t>(x),
        .top = static_cast<int32_t>(y),
        .right = static_cast<int32_t>(x + width),
        .bottom = static_cast<int32_t>(y + height),
      };
      return view;
    };

    auto consumer_comp = vortex::CompositionView::ForScene(
      main_view_id_, make_view(0.0F, 0.0F, half_w, half_h), main_camera_node_);
    consumer_comp.name = "M06A.AuxConsumer.Main";
    consumer_comp.with_atmosphere = true;
    consumer_comp.with_height_fog = true;
    consumer_comp.clear_color = graphics::Color { 0.05F, 0.09F, 0.16F, 1.0F };
    consumer_comp.consumed_aux_outputs.push_back(
      vortex::CompositionView::AuxInputDesc {
        .id = vortex::CompositionView::AuxOutputId { 7001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .required = true,
      });
    main_view_ = consumer_comp.id;
    views.push_back(std::move(consumer_comp));

    auto producer_comp = vortex::CompositionView::ForScene(debug_view_id_,
      make_view(half_w, 0.0F, sw - half_w, half_h), debug_camera_node_);
    producer_comp.name = "M06A.AuxProducer.Color";
    producer_comp.view_kind = vortex::CompositionView::ViewKind::kAuxiliary;
    producer_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kWorldNormals;
    producer_comp.clear_color = graphics::Color { 0.09F, 0.04F, 0.12F, 1.0F };
    producer_comp.produced_aux_outputs.push_back(
      vortex::CompositionView::AuxOutputDesc {
        .id = vortex::CompositionView::AuxOutputId { 7001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .debug_name = "M06A.AuxProducer.ColorTexture",
      });
    views.push_back(std::move(producer_comp));

    auto top_comp = vortex::CompositionView::ForScene(top_view_id_,
      make_view(0.0F, half_h, half_w, sh - half_h), top_camera_node_);
    top_comp.name = "M06A.AuxProof.WireframeTop";
    top_comp.force_wireframe = true;
    top_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    top_comp.clear_color = graphics::Color { 0.02F, 0.08F, 0.06F, 1.0F };
    views.push_back(std::move(top_comp));

    auto shadow_comp = vortex::CompositionView::ForScene(shadow_view_id_,
      make_view(half_w, half_h, sw - half_w, sh - half_h), shadow_camera_node_);
    shadow_comp.name = "M06A.AuxProof.DirectionalShadowMask";
    shadow_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kDirectionalShadowMask;
    shadow_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kShadows
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    shadow_comp.clear_color = graphics::Color { 0.08F, 0.07F, 0.02F, 1.0F };
    views.push_back(std::move(shadow_comp));
    return;
  }

  if (config_.proof_layout) {
    const float half_w = std::floor(sw * 0.5F);
    const float half_h = std::floor(sh * 0.5F);
    const auto make_view = [](const float x, const float y, const float width,
                             const float height) -> View {
      View view {};
      view.viewport = ViewPort {
        .top_left_x = x,
        .top_left_y = y,
        .width = width,
        .height = height,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
      };
      view.scissor = Scissors {
        .left = static_cast<int32_t>(x),
        .top = static_cast<int32_t>(y),
        .right = static_cast<int32_t>(x + width),
        .bottom = static_cast<int32_t>(y + height),
      };
      return view;
    };

    auto lit_comp = vortex::CompositionView::ForScene(
      main_view_id_, make_view(0.0F, 0.0F, half_w, half_h), main_camera_node_);
    lit_comp.name = "M06A.LitPerspective";
    lit_comp.with_atmosphere = true;
    lit_comp.with_height_fog = true;
    lit_comp.clear_color = graphics::Color { 0.05F, 0.11F, 0.20F, 1.0F };
    lit_comp.produced_aux_outputs.push_back(
      vortex::CompositionView::AuxOutputDesc {
        .id = vortex::CompositionView::AuxOutputId { 6001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .debug_name = "M06A.LitPerspective.Color",
      });
    main_view_ = lit_comp.id;
    views.push_back(std::move(lit_comp));

    auto top_comp = vortex::CompositionView::ForScene(top_view_id_,
      make_view(half_w, 0.0F, sw - half_w, half_h), top_camera_node_);
    top_comp.name = "M06A.WireframeTop";
    top_comp.force_wireframe = true;
    top_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    top_comp.clear_color = graphics::Color { 0.02F, 0.08F, 0.06F, 1.0F };
    views.push_back(std::move(top_comp));

    auto debug_comp = vortex::CompositionView::ForScene(debug_view_id_,
      make_view(0.0F, half_h, half_w, sh - half_h), debug_camera_node_);
    debug_comp.name = "M06A.WorldNormals";
    debug_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kWorldNormals;
    debug_comp.consumed_aux_outputs.push_back(
      vortex::CompositionView::AuxInputDesc {
        .id = vortex::CompositionView::AuxOutputId { 6001U },
        .kind = vortex::CompositionView::AuxOutputKind::kColorTexture,
        .required = true,
      });
    debug_comp.clear_color = graphics::Color { 0.10F, 0.04F, 0.10F, 1.0F };
    views.push_back(std::move(debug_comp));

    auto shadow_comp = vortex::CompositionView::ForScene(shadow_view_id_,
      make_view(half_w, half_h, sw - half_w, sh - half_h), shadow_camera_node_);
    shadow_comp.name = "M06A.DirectionalShadowMask";
    shadow_comp.render_settings.shader_debug_mode
      = vortex::ShaderDebugMode::kDirectionalShadowMask;
    shadow_comp.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      vortex::CompositionView::ViewFeatureMask::kSceneLighting
      | vortex::CompositionView::ViewFeatureMask::kShadows
      | vortex::CompositionView::ViewFeatureMask::kDiagnostics
    };
    shadow_comp.clear_color = graphics::Color { 0.08F, 0.07F, 0.02F, 1.0F };
    views.push_back(std::move(shadow_comp));
    return;
  }

  // 1. Main Scene (Full screen)
  View main_view {};
  main_view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = sw,
    .height = sh,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  main_view.scissor = Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(extent.width),
    .bottom = static_cast<int32_t>(extent.height),
  };
  auto main_comp = vortex::CompositionView::ForScene(
    main_view_id_, main_view, main_camera_node_);
  main_comp.with_atmosphere = true;
  const bool pip_only
    = config_.exposure_proof == ExposureProofScenario::kPipOnly
    || (config_.exposure_proof == ExposureProofScenario::kSourceLoss
      && context.GetFrameSequenceNumber().get() >= 44U)
    || (config_.exposure_proof == ExposureProofScenario::kInteractions
      && context.GetFrameSequenceNumber().get() >= 120U
      && context.GetFrameSequenceNumber().get() < 128U);
  if (!pip_only) {
    main_view_ = main_comp.id;
  }
  const graphics::Color kMainClearColor { 0.1F, 0.2F, 0.38F, 1.0F };
  main_comp.clear_color = kMainClearColor;
  views.push_back(std::move(main_comp));

  // 2. PiP View
  if (pip_camera_node_.IsAlive()) {
    const auto pip_layout = ComputePipLayout(extent, viewport_proof_resized_);

    View pip_view {};
    pip_view.viewport = ViewPort {
      .top_left_x = pip_layout.offset_x,
      .top_left_y = pip_layout.offset_y,
      .width = pip_layout.width,
      .height = pip_layout.height,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    pip_view.scissor = Scissors {
      .left = static_cast<int32_t>(pip_layout.offset_x),
      .top = static_cast<int32_t>(pip_layout.offset_y),
      .right = static_cast<int32_t>(pip_layout.offset_x + pip_layout.width),
      .bottom = static_cast<int32_t>(pip_layout.offset_y + pip_layout.height),
    };

    const auto scissor_inset
      = viewport_proof_scissored_ ? 96U : config_.pip_scissor_inset_px;
    if (scissor_inset > 0U) {
      const auto inset = static_cast<int32_t>(scissor_inset);
      pip_view.scissor.left
        = (std::min)(pip_view.scissor.left + inset, pip_view.scissor.right);
      pip_view.scissor.top
        = (std::min)(pip_view.scissor.top + inset, pip_view.scissor.bottom);
      pip_view.scissor.right
        = (std::max)(pip_view.scissor.left, pip_view.scissor.right - inset);
      pip_view.scissor.bottom
        = (std::max)(pip_view.scissor.top, pip_view.scissor.bottom - inset);
      CHECK_F(pip_view.scissor.left < pip_view.scissor.right
          && pip_view.scissor.top < pip_view.scissor.bottom,
        "PiP scissor inset {} collapses the PiP depth rect (viewport={}x{})",
        scissor_inset, pip_layout.width, pip_layout.height);
    }

    auto pip_comp = vortex::CompositionView::ForPip(pip_view_id_,
      vortex::CompositionView::ZOrder {
        vortex::CompositionView::kZOrderScene.get() + 1 },
      pip_view, pip_camera_node_);

    const graphics::Color kPipClearColor { 0.1F, 0.1F, 0.1F, 0.5F };
    pip_comp.clear_color = kPipClearColor;
    pip_comp.opacity = 1.0F;
    pip_comp.force_wireframe = config_.pip_force_wireframe;
    if (pip_only) {
      main_view_ = pip_comp.id;
    }

    views.push_back(std::move(pip_comp));
  }
}

auto MainModule::RetainInactiveView(const ViewId view_id) const noexcept -> bool
{
  return retain_inactive_pip_ && view_id == pip_view_id_;
}

} // namespace oxygen::examples::multiview
