//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>

#include "DemoShell/Runtime/AppWindow.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "VortexBasic/MainModule.h"

using oxygen::ViewPort;
using oxygen::data::Vertex;
using oxygen::scene::PerspectiveCamera;

namespace {

constexpr uint32_t kDefaultOffscreenWidth = 1280U;
constexpr uint32_t kDefaultOffscreenHeight = 720U;
constexpr glm::vec3 kFloorCenter { 0.0F, 0.0F, -0.125F };
constexpr glm::vec3 kFloorScale { 20.0F, 20.0F, 0.25F };
constexpr glm::vec4 kFloorColor { 0.02F, 0.02F, 0.03F, 1.0F };
constexpr glm::vec3 kCubeCenter { 0.0F, 0.0F, 2.0F };
constexpr glm::vec4 kCubeColor { 0.82F, 0.80F, 0.74F, 1.0F };
constexpr glm::vec3 kSceneFocusPoint { 0.0F, 0.0F, 1.0F };
constexpr float kDirectionalLightOrbitPeriodSeconds = 10.0F;
constexpr float kDirectionalLightOrbitRadiusX = 18.0F;
constexpr float kDirectionalLightOrbitRadiusY = 12.0F;
constexpr float kDirectionalLightBaseHeight = 12.0F;
constexpr float kDirectionalLightHeightVariation = 6.0F;
constexpr float kPointLightOrbitRadius = 2.0F;
constexpr float kPointLightOrbitPeriodSeconds = 4.0F;
constexpr float kSpotlightOscillationAmplitude = 3.0F;
constexpr float kSpotlightOscillationPeriodSeconds = 6.0F;
constexpr float kCubeRotationAnglePerSecond = glm::radians(20.0F);

auto RandomUnitAxis(std::mt19937& rng) -> glm::vec3
{
  std::uniform_real_distribution<float> component_dist(-1.0F, 1.0F);
  auto axis = glm::vec3(0.0F);
  while (true) {
    axis = glm::vec3 {
      component_dist(rng),
      component_dist(rng),
      component_dist(rng),
    };
    if (glm::dot(axis, axis) > oxygen::math::Epsilon) {
      return glm::normalize(axis);
    }
  }
}

auto NormalizeOrFallback(const glm::vec3& direction, const glm::vec3& fallback)
  -> glm::vec3
{
  const auto length_sq = glm::dot(direction, direction);
  if (length_sq <= oxygen::math::Epsilon) {
    return fallback;
  }
  return direction / std::sqrt(length_sq);
}

auto RotationFromDirToDir(const glm::vec3& from_dir,
  const glm::vec3& fallback_dir, const glm::vec3& up_axis,
  const glm::vec3& direction) -> glm::quat
{
  const auto to_dir = NormalizeOrFallback(direction, fallback_dir);
  const auto cos_theta = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);

  if (cos_theta >= 0.9999F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  if (cos_theta <= -0.9999F) {
    return glm::angleAxis(oxygen::math::Pi, up_axis);
  }

  const auto axis = glm::normalize(glm::cross(from_dir, to_dir));
  const auto angle = std::acos(cos_theta);
  return glm::angleAxis(angle, axis);
}

auto LookRotation(const glm::vec3& position, const glm::vec3& target)
  -> glm::quat
{
  return RotationFromDirToDir(oxygen::space::move::Forward,
    oxygen::space::move::Forward, oxygen::space::move::Up, target - position);
}

auto CameraLookRotation(const glm::vec3& position, const glm::vec3& target,
  const glm::vec3& up_direction = oxygen::space::move::Up) -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return { 1.0F, 0.0F, 0.0F, 0.0F };
  }

  const auto forward = glm::normalize(forward_raw);
  glm::vec3 up_dir = up_direction;
  const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
  if (dot_abs > 0.999F) {
    up_dir = (std::abs(forward.z) > 0.9F) ? oxygen::space::move::Back
                                          : oxygen::space::move::Up;
  }

  const auto right_raw = glm::cross(forward, up_dir);
  const float right_len2 = glm::dot(right_raw, right_raw);
  if (right_len2 <= std::numeric_limits<float>::epsilon()) {
    return { 1.0F, 0.0F, 0.0F, 0.0F };
  }

  const auto right = right_raw / std::sqrt(right_len2);
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  return glm::quat_cast(look_matrix);
}

auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  const float roughness = 0.9F, const float metalness = 0.0F)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::render::MaterialAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(d::AssetType::kMaterial);
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(d::MaterialDomain::kOpaque);
  desc.flags = pak::render::kMaterialFlag_DoubleSided;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { metalness };
  desc.roughness = d::Unorm16 { roughness };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  const auto key = d::AssetKey::FromVirtualPath(
    "/Engine/Examples/VortexBasic/Materials/" + std::string(name) + ".omat");
  return std::make_shared<const d::MaterialAsset>(
    key, desc, std::vector<d::ShaderReference> {});
  // NOLINTEND(*-magic-numbers)
}

auto BuildCubeGeometry(const char* geometry_name, const char* material_name,
  const glm::vec4& rgba, const float roughness = 0.9F,
  const float metalness = 0.0F) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  namespace d = oxygen::data;
  namespace pak = d::pak;

  auto cube_data = d::MakeCubeMeshAsset();
  CHECK_F(cube_data.has_value());

  const auto material
    = MakeSolidColorMaterial(material_name, rgba, roughness, metalness);

  auto mesh
    = d::MeshBuilder(0, geometry_name)
        .WithVertices(cube_data->first)
        .WithIndices(cube_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(pak::geometry::MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(cube_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  pak::geometry::GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  return std::make_shared<d::GeometryAsset>(
    d::AssetKey::FromVirtualPath("/Engine/Examples/VortexBasic/Geometry/"
      + std::string(geometry_name) + ".ogeo"),
    geo_desc, std::vector<std::shared_ptr<d::Mesh>> { std::move(mesh) });
}

} // namespace

namespace oxygen::examples::vortex_basic {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MainModule::MainModule(const DemoAppContext& app,
  const vortex::ShaderDebugMode shader_debug_mode) noexcept
  : app_(app)
  , shader_debug_mode_(shader_debug_mode)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());

  if (!app_.headless) {
    auto& wnd = AddComponent<AppWindow>(app_);
    app_window_ = observer_ptr(&wnd);
  }
}

MainModule::~MainModule()
{
  renderer_subscription_.Cancel();
  scene_.reset();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto MainModule::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool
{
  DCHECK_NOTNULL_F(engine);

  if (!app_.headless) {
    DCHECK_NOTNULL_F(app_window_);

    platform::window::Properties props("Vortex Basic Example");
    constexpr uint32_t kWidth = 1280U;
    constexpr uint32_t kHeight = 720U;
    props.extent = { .width = kWidth, .height = kHeight };
    props.flags = {
      .hidden = false,
      .always_on_top = false,
      .full_screen = app_.fullscreen,
      .maximized = false,
      .minimized = false,
      .resizable = true,
      .borderless = false,
    };
    if (!app_window_->CreateAppWindow(props)) {
      LOG_F(ERROR, "VortexBasic: could not create application window");
      return false;
    }
  }

  main_view_id_ = ViewId { s_next_view_id_++ };

  // Subscribe to Vortex Renderer attachment so we can hold an observer.
  renderer_subscription_ = engine->SubscribeModuleAttached(
    [this](const engine::ModuleEvent& event) {
      if (event.type_id != vortex::Renderer::ClassTypeId()) {
        return;
      }
      vortex_renderer_
        = observer_ptr { static_cast<vortex::Renderer*>(event.module.get()) };
    },
    true);

  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  renderer_subscription_.Cancel();
  ReleasePublishedRuntimeView();
  ClearSceneFb();
  camera_node_ = {};
  cube_node_ = {};
  scene_.reset();
  vortex_renderer_.reset(nullptr);
}

auto MainModule::ReleasePublishedRuntimeView(
  const observer_ptr<engine::FrameContext> context) -> void
{
  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    return;
  }

  renderer->SetShaderDebugMode(vortex::ShaderDebugMode::kDisabled);
  if (main_view_id_ == kInvalidViewId) {
    return;
  }

  if (context != nullptr) {
    renderer->RemovePublishedRuntimeView(*context, main_view_id_);
    return;
  }

  renderer->RemovePublishedRuntimeView(main_view_id_);
}

auto MainModule::ResolveVortexRenderer() -> observer_ptr<vortex::Renderer>
{
  if (app_.engine) {
    if (auto renderer = app_.engine->GetModule<vortex::Renderer>()) {
      vortex_renderer_ = observer_ptr { &renderer->get() };
      return vortex_renderer_;
    }
  }
  vortex_renderer_.reset(nullptr);
  return nullptr;
}

auto MainModule::BuildResolvedView(const uint32_t width, const uint32_t height)
  -> std::optional<ResolvedView>
{
  if (!camera_node_.IsAlive() || !camera_node_.HasCamera()) {
    return std::nullopt;
  }

  const auto viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  auto camera_node = camera_node_;
  auto resolver = vortex::SceneCameraViewResolver {
    [camera_node](const ViewId& /*unused*/) { return camera_node; },
    viewport,
  };
  return resolver(main_view_id_);
}

// ---------------------------------------------------------------------------
// Frame phases
// ---------------------------------------------------------------------------

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);

  if (scene_
    && (app_.headless
      || (app_window_ != nullptr && app_window_->GetWindow() != nullptr
        && !app_window_->IsShuttingDown()))) {
    context->SetScene(observer_ptr { scene_.get() });
  }

  if (app_.headless || !app_window_) {
    return;
  }

  // Handle window destruction.
  if (!app_window_->GetWindow()) {
    if (last_surface_) {
      const auto surfaces = context->GetSurfaces();
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i] == last_surface_) {
          context->RemoveSurfaceAt(i);
          break;
        }
      }
      last_surface_ = nullptr;
    }
    ReleasePublishedRuntimeView(context);
    return;
  }

  // Handle resize.
  if (app_window_->ShouldResize()) {
    ClearSceneFb();
    app_window_->ApplyPendingResize();
  }

  // Register the surface with the frame context.
  auto surface = app_window_->GetSurface().lock();
  if (surface) {
    auto surfaces = context->GetSurfaces();
    const bool already_registered = std::ranges::any_of(
      surfaces, [&](const auto& s) { return s.get() == surface.get(); });
    if (!already_registered) {
      context->AddSurface(observer_ptr { surface.get() });
    }
    last_surface_ = observer_ptr { surface.get() };
  } else {
    last_surface_ = nullptr;
  }
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!app_.headless && (!app_window_ || !app_window_->GetWindow())) {
    co_return;
  }

  EnsureScene();
  EnsureLighting();
  const auto extent = ResolveViewExtent();
  EnsureCamera(extent.x, extent.y);
  UpdateValidationScene(context);
  if (scene_) {
    scene_->Update(false);
  }
  co_return;
}

auto MainModule::OnPublishViews(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  auto renderer = ResolveVortexRenderer();
  if (!renderer) {
    co_return;
  }
  if (!app_.headless && (!app_window_ || !app_window_->GetWindow())) {
    co_return;
  }
  if (!camera_node_.IsAlive()) {
    co_return;
  }

  const auto extent = ResolveViewExtent();
  if (extent.x == 0 || extent.y == 0) {
    co_return;
  }

  EnsureSceneFb(extent.x, extent.y);
  if (!scene_fb_) {
    co_return;
  }

  renderer->SetShaderDebugMode(shader_debug_mode_);

  // Build the ViewContext for the Vortex Renderer.
  engine::ViewContext view_ctx {};
  view_ctx.view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.x),
    .height = static_cast<float>(extent.y),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  view_ctx.metadata.name = "MainView";
  view_ctx.metadata.purpose = "primary";
  view_ctx.metadata.is_scene_view = true;
  view_ctx.metadata.with_atmosphere = app_.with_atmosphere;
  view_ctx.metadata.with_height_fog = app_.with_height_fog;
  view_ctx.metadata.with_local_fog = app_.with_local_fog;
  view_ctx.render_target = observer_ptr { scene_fb_.get() };
  view_ctx.composite_source = observer_ptr { scene_fb_.get() };

  renderer->UpsertPublishedRuntimeView(*context, main_view_id_,
    std::move(view_ctx), vortex::ShadingMode::kDeferred);
  const auto published_view_id
    = renderer->ResolvePublishedRuntimeViewId(main_view_id_);
  if (published_view_id != kInvalidViewId) {
    if (const auto resolved_view = BuildResolvedView(extent.x, extent.y);
      resolved_view.has_value()) {
      renderer->RegisterResolvedView(
        published_view_id, std::move(*resolved_view));
    }
  }
  co_return;
}

auto MainModule::OnCompositing(observer_ptr<engine::FrameContext> /*context*/)
  -> co::Co<>
{
  auto renderer = ResolveVortexRenderer();
  if (app_.headless || !renderer || !app_window_ || !app_window_->GetWindow()) {
    co_return;
  }

  auto target_fb = app_window_->GetCurrentFrameBuffer().lock();
  if (!target_fb) {
    co_return;
  }

  auto surface = app_window_->GetSurface().lock();
  if (!surface) {
    co_return;
  }

  const auto extent = app_window_->GetWindow()->Size();
  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.width),
    .height = static_cast<float>(extent.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  // Resolve the published view id (intent → stable published id).
  const auto published_id
    = renderer->ResolvePublishedRuntimeViewId(main_view_id_);
  if (published_id == kInvalidViewId) {
    co_return;
  }

  vortex::CompositionSubmission submission {};
  submission.composite_target = target_fb;
  submission.tasks.push_back(
    vortex::CompositingTask::MakeCopy(published_id, viewport));

  renderer->RegisterComposition(std::move(submission), std::move(surface));
  co_return;
}

// ---------------------------------------------------------------------------
// Scene setup
// ---------------------------------------------------------------------------

auto MainModule::EnsureScene() -> void
{
  if (scene_) {
    return;
  }

  LOG_SCOPE_FUNCTION(INFO);

  constexpr size_t kCapacity = 32;
  scene_ = std::make_shared<scene::Scene>("VortexBasicScene", kCapacity);
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());

  if (const auto environment = scene_->GetEnvironment();
    environment != nullptr) {
    if (!environment->HasSystem<scene::environment::SkyAtmosphere>()) {
      auto& atmosphere
        = environment->AddSystem<scene::environment::SkyAtmosphere>();
      atmosphere.SetEnabled(true);
      atmosphere.SetTransformMode(scene::environment::
          SkyAtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin);
      atmosphere.SetRenderInMainPass(true);
      atmosphere.SetSkyLuminanceFactorRgb({ 1.0F, 1.0F, 1.0F });
      atmosphere.SetSkyAndAerialPerspectiveLuminanceFactorRgb(
        { 1.0F, 1.0F, 1.0F });
      atmosphere.SetAerialPerspectiveDistanceScale(1.0F);
      atmosphere.SetAerialScatteringStrength(1.0F);
      atmosphere.SetAerialPerspectiveStartDepthMeters(100.0F);
      atmosphere.SetHeightFogContribution(1.0F);
      atmosphere.SetTraceSampleCountScale(1.0F);
      atmosphere.SetSunDiskEnabled(true);
    }
    if (app_.with_height_fog
      && !environment->HasSystem<scene::environment::Fog>()) {
      auto& fog = environment->AddSystem<scene::environment::Fog>();
      fog.SetEnabled(true);
      fog.SetEnableHeightFog(true);
      fog.SetEnableVolumetricFog(false);
      fog.SetExtinctionSigmaTPerMeter(0.0015F);
      fog.SetHeightFalloffPerMeter(0.12F);
      fog.SetHeightOffsetMeters(0.0F);
      fog.SetStartDistanceMeters(0.0F);
      fog.SetMaxOpacity(1.0F);
      fog.SetFogInscatteringLuminance({ 0.3F, 0.38F, 0.48F });
      fog.SetSkyAtmosphereAmbientContributionColorScale({ 1.0F, 1.0F, 1.0F });
      fog.SetDirectionalInscatteringLuminance({ 1.0F, 0.95F, 0.9F });
      fog.SetDirectionalInscatteringExponent(8.0F);
      fog.SetDirectionalInscatteringStartDistance(0.0F);
    }
  }

  auto cube_geo = BuildCubeGeometry(
    "ValidationCube", "ValidationCube", kCubeColor, 0.28F, 0.85F);
  auto floor_geo = BuildCubeGeometry(
    "ValidationFloor", "ValidationFloor", kFloorColor, 0.90F);

  cube_node_ = scene_->CreateNode("Cube");
  cube_node_.GetRenderable().SetGeometry(std::move(cube_geo));
  cube_node_.GetTransform().SetLocalPosition(kCubeCenter);
  cube_rotation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  cube_rotation_axis_ = RandomUnitAxis(cube_rotation_rng_);
  cube_node_.GetTransform().SetLocalRotation(cube_rotation_);

  floor_node_ = scene_->CreateNode("Floor");
  floor_node_.GetRenderable().SetGeometry(std::move(floor_geo));
  floor_node_.GetTransform().SetLocalScale(kFloorScale);
  floor_node_.GetTransform().SetLocalPosition(kFloorCenter);

  if (app_.with_local_fog) {
    local_fog_volume_node_ = scene_->CreateNode("LocalFogVolume");
    if (const auto impl = local_fog_volume_node_.GetImpl(); impl.has_value()) {
      impl->get().AddComponent<scene::environment::LocalFogVolume>();
      auto& local_fog
        = impl->get().GetComponent<scene::environment::LocalFogVolume>();
      local_fog.SetEnabled(true);
      local_fog.SetRadialFogExtinction(2.60F);
      local_fog.SetHeightFogExtinction(1.40F);
      local_fog.SetHeightFogFalloff(0.22F);
      local_fog.SetHeightFogOffset(-0.5F);
      local_fog.SetFogPhaseG(0.15F);
      local_fog.SetFogAlbedo({ 0.10F, 0.92F, 0.86F });
      local_fog.SetFogEmissive({ 7.5F, 0.4F, 5.5F });
      local_fog.SetSortPriority(2);
    }
    local_fog_volume_node_.GetTransform().SetLocalPosition(
      { 0.0F, 0.0F, 1.6F });
    local_fog_volume_node_.GetTransform().SetLocalScale({ 8.0F, 8.0F, 8.0F });
  }
}

auto MainModule::EnsureLighting() -> void
{
  if (!scene_) {
    return;
  }

  if (!directional_light_node_.IsAlive()) {
    directional_light_node_ = scene_->CreateNode("SunLight");
    auto light = std::make_unique<scene::DirectionalLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 1.0F, 0.97F, 0.92F };
    light->SetAngularSizeRadians(glm::radians(0.53F));
    light->SetIntensityLux(100000.0F);
    light->SetEnvironmentContribution(true);
    light->SetIsSunLight(true);
    light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    light->SetUsePerPixelAtmosphereTransmittance(true);
    light->SetAtmosphereDiskLuminanceScale({ 1.0F, 0.95F, 0.9F });
    CHECK_F(directional_light_node_.AttachLight(std::move(light)),
      "Failed to attach DirectionalLight to SunLight");
  }

  if (!point_light_node_.IsAlive()) {
    point_light_node_ = scene_->CreateNode("PointFill");
    auto light = std::make_unique<scene::PointLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 0.30F, 0.56F, 1.0F };
    light->SetLuminousFluxLm(1200.0F);
    light->SetRange(3.0F);
    CHECK_F(point_light_node_.AttachLight(std::move(light)),
      "Failed to attach PointLight to PointFill");
  }

  if (!spot_light_node_.IsAlive()) {
    spot_light_node_ = scene_->CreateNode("SpotRim");
    auto light = std::make_unique<scene::SpotLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 1.0F, 0.15F, 0.1F };
    light->SetLuminousFluxLm(12000.0F);
    light->SetRange(10.0F);
    light->SetInnerConeAngleRadians(glm::radians(40.0F));
    light->SetOuterConeAngleRadians(glm::radians(55.0F));
    CHECK_F(spot_light_node_.AttachLight(std::move(light)),
      "Failed to attach SpotLight to SpotRim");
  }
}

auto MainModule::UpdateValidationScene(
  const observer_ptr<engine::FrameContext> context) -> void
{
  const auto delta_seconds = context != nullptr
    ? std::chrono::duration<float>(context->GetGameDeltaTime().get()).count()
    : 0.0F;
  animation_time_seconds_
    += delta_seconds > 0.0F ? delta_seconds : (1.0F / 60.0F);

  if (cube_node_.IsAlive()) {
    const float rotation_step_radians = kCubeRotationAnglePerSecond
      * (delta_seconds > 0.0F ? delta_seconds : (1.0F / 60.0F));
    cube_rotation_ = glm::normalize(
      glm::angleAxis(rotation_step_radians, cube_rotation_axis_)
      * cube_rotation_);
    cube_node_.GetTransform().SetLocalPosition(kCubeCenter);
    cube_node_.GetTransform().SetLocalRotation(cube_rotation_);
  }

  if (point_light_node_.IsAlive()) {
    const auto orbit_angle = animation_time_seconds_
      * (oxygen::math::TwoPi / kPointLightOrbitPeriodSeconds);
    const auto point_light_offset = oxygen::space::move::Right
        * (std::cos(orbit_angle) * kPointLightOrbitRadius)
      + oxygen::space::move::Back
        * (std::sin(orbit_angle) * kPointLightOrbitRadius);
    point_light_node_.GetTransform().SetLocalPosition(
      kCubeCenter + point_light_offset);
  }

  if (spot_light_node_.IsAlive()) {
    const auto oscillation_angle = animation_time_seconds_
      * (oxygen::math::TwoPi / kSpotlightOscillationPeriodSeconds);
    const auto position = glm::vec3 {
      std::sin(oscillation_angle) * kSpotlightOscillationAmplitude,
      4.0F,
      4.0F,
    };
    spot_light_node_.GetTransform().SetLocalPosition(position);
    spot_light_node_.GetTransform().SetLocalRotation(
      LookRotation(position, kCubeCenter));
  }

  if (directional_light_node_.IsAlive()) {
    const auto orbit_angle = animation_time_seconds_
      * (oxygen::math::TwoPi / kDirectionalLightOrbitPeriodSeconds);
    const auto sun_position = oxygen::space::move::Right
        * (std::cos(orbit_angle) * kDirectionalLightOrbitRadiusX)
      + oxygen::space::move::Back
        * (std::sin(orbit_angle) * kDirectionalLightOrbitRadiusY)
      + oxygen::space::move::Up
        * (kDirectionalLightBaseHeight
          + std::sin(orbit_angle) * kDirectionalLightHeightVariation);
    directional_light_node_.GetTransform().SetLocalPosition(sun_position);
    directional_light_node_.GetTransform().SetLocalRotation(
      LookRotation(sun_position, kSceneFocusPoint));
  }
}

auto MainModule::ResolveViewExtent() const noexcept -> glm::uvec2
{
  if (!app_.headless && app_window_ && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    return { extent.width, extent.height };
  }

  return { kDefaultOffscreenWidth, kDefaultOffscreenHeight };
}

auto MainModule::EnsureCamera(uint32_t width, uint32_t height) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  if (!scene_) {
    return;
  }

  if (!camera_node_.IsAlive()) {
    camera_node_ = scene_->CreateNode("MainCamera");
  }

  if (!camera_node_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool ok = camera_node_.AttachCamera(std::move(camera));
    CHECK_F(ok, "Failed to attach PerspectiveCamera");
  }

  constexpr glm::vec3 kCameraPosition { 0.0F, 8.0F, 4.0F };
  constexpr glm::vec3 kCameraTarget { 0.0F, 0.0F, 1.5F };
  camera_node_.GetTransform().SetLocalPosition(kCameraPosition);
  camera_node_.GetTransform().SetLocalRotation(
    CameraLookRotation(kCameraPosition, kCameraTarget));

  const auto cam_ref = camera_node_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(55.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(100.0F);
    cam.SetViewport(ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
  }
  // NOLINTEND(*-magic-numbers)
}

// ---------------------------------------------------------------------------
// Intermediate framebuffer management
// ---------------------------------------------------------------------------

auto MainModule::EnsureSceneFb(uint32_t width, uint32_t height) -> void
{
  if (scene_fb_ && scene_fb_width_ == width && scene_fb_height_ == height) {
    return;
  }

  ClearSceneFb();

  auto gfx = app_.gfx_weak.lock();
  if (!gfx) {
    return;
  }

  graphics::TextureDesc color_desc {};
  color_desc.width = width;
  color_desc.height = height;
  color_desc.format = Format::kRGBA8UNorm;
  color_desc.texture_type = TextureType::kTexture2D;
  color_desc.is_render_target = true;
  color_desc.is_shader_resource = true;
  color_desc.initial_state = graphics::ResourceStates::kCommon;
  color_desc.use_clear_value = true;
  color_desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  color_desc.debug_name = "VortexBasic.SceneColor";

  auto color_tex = gfx->CreateTexture(color_desc);
  CHECK_F(static_cast<bool>(color_tex), "Failed to create scene color texture");

  graphics::FramebufferDesc fb_desc {};
  fb_desc.AddColorAttachment({ .texture = std::move(color_tex) });
  scene_fb_ = gfx->CreateFramebuffer(fb_desc);
  CHECK_F(static_cast<bool>(scene_fb_), "Failed to create scene framebuffer");

  scene_fb_width_ = width;
  scene_fb_height_ = height;
}

auto MainModule::ClearSceneFb() -> void
{
  scene_fb_.reset();
  scene_fb_width_ = 0;
  scene_fb_height_ = 0;
}

} // namespace oxygen::examples::vortex_basic
