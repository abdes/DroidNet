//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
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
#include <Oxygen/Core/Types/PostProcess.h>
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
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneCameraViewResolver.h>
#include <Oxygen/Vortex/Types/CompositingTask.h>

#include "DemoShell/Runtime/AppWindow.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/DefaultSceneLighting.h"
#include "VortexBasic/MainModule.h"
#include "VortexBasic/NormalMapValidationTexture.h"

using oxygen::ViewPort;
using oxygen::data::Vertex;
using oxygen::scene::PerspectiveCamera;

namespace {

constexpr uint32_t kDefaultOffscreenWidth = 1920U;
constexpr uint32_t kDefaultOffscreenHeight = 1440U;
constexpr glm::vec3 kFloorCenter { 0.0F, 0.0F, -0.125F };
constexpr glm::vec3 kFloorScale { 60.0F, 60.0F, 0.25F };
constexpr glm::vec4 kFloorColor { 0.02F, 0.02F, 0.03F, 1.0F };
constexpr glm::vec3 kCubeCenter { 0.0F, 0.0F, 2.0F };
constexpr glm::vec3 kOcclusionProbeCenter { 0.0F, -1.45F, 1.55F };
constexpr glm::vec3 kOcclusionProbeScale { 0.45F, 0.45F, 0.45F };
constexpr glm::vec3 kTranslucentSphereCenter { 0.0F, 5.20F, 3.15F };
constexpr glm::vec3 kTranslucentCylinderCenter { 1.65F, 4.80F, 2.20F };
constexpr glm::vec3 kTranslucentSphereScale { 0.85F, 0.85F, 0.85F };
constexpr glm::vec3 kTranslucentCylinderScale { 0.48F, 0.48F, 1.05F };
constexpr glm::vec4 kCubeColor { 0.82F, 0.80F, 0.74F, 1.0F };
constexpr glm::vec4 kOcclusionProbeColor { 1.0F, 0.12F, 0.08F, 1.0F };
constexpr glm::vec4 kTranslucentSphereColor { 0.00F, 0.95F, 1.0F, 0.18F };
constexpr glm::vec4 kTranslucentCylinderColor { 1.0F, 0.10F, 0.82F, 0.16F };
constexpr glm::vec3 kSceneFocusPoint { 0.0F, 0.0F, 1.0F };
constexpr glm::vec3 kSunPosition { -10.0F, 10.0F, 16.0F };
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

auto SetShadowParticipation(oxygen::scene::SceneNode& node,
  const bool casts_shadows, const bool receives_shadows) -> void
{
  if (auto flags_ref = node.GetFlags(); flags_ref.has_value()) {
    auto& flags = flags_ref->get();
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(receives_shadows));
  }
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
  const float roughness = 0.9F, const float metalness = 0.0F,
  const oxygen::data::MaterialDomain domain
  = oxygen::data::MaterialDomain::kOpaque,
  const glm::vec3 emissive = glm::vec3 { 0.0F },
  const uint32_t extra_flags = 0U, const bool double_sided = true,
  const oxygen::content::ResourceKey normal_map = {})
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
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = pak::render::kMaterialFlag_NoTextureSampling | extra_flags;
  if (double_sided) {
    desc.flags |= pak::render::kMaterialFlag_DoubleSided;
  }
  if (normal_map != oxygen::content::ResourceKey {}) {
    desc.flags &= ~pak::render::kMaterialFlag_NoTextureSampling;
    desc.base_color_texture = pak::core::kFallbackResourceIndex;
  }
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { metalness };
  desc.roughness = d::Unorm16 { roughness };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  desc.emissive_factor[0] = d::HalfFloat { emissive.r };
  desc.emissive_factor[1] = d::HalfFloat { emissive.g };
  desc.emissive_factor[2] = d::HalfFloat { emissive.b };
  const auto key = d::AssetKey::FromVirtualPath(
    "/Engine/Examples/VortexBasic/Materials/" + std::string(name) + ".omat");
  return std::make_shared<const d::MaterialAsset>(key, desc,
    std::vector<d::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { {}, normal_map });
  // NOLINTEND(*-magic-numbers)
}

auto BuildPrimitiveGeometry(const char* geometry_name,
  const char* material_name, std::vector<Vertex> vertices,
  std::vector<uint32_t> indices, const glm::vec4& rgba,
  const float roughness = 0.9F, const float metalness = 0.0F,
  const oxygen::data::MaterialDomain domain
  = oxygen::data::MaterialDomain::kOpaque,
  const glm::vec3 emissive = glm::vec3 { 0.0F },
  const uint32_t extra_material_flags = 0U, const bool double_sided = true,
  const oxygen::content::ResourceKey normal_map = {})
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  namespace d = oxygen::data;
  namespace pak = d::pak;

  const auto material
    = MakeSolidColorMaterial(material_name, rgba, roughness, metalness, domain,
      emissive, extra_material_flags, double_sided, normal_map);

  const auto vertex_count = static_cast<uint32_t>(vertices.size());
  const auto index_count = static_cast<uint32_t>(indices.size());
  auto mesh = d::MeshBuilder(0, geometry_name)
                .WithVertices(std::move(vertices))
                .WithIndices(std::move(indices))
                .BeginSubMesh("full", material)
                .WithMeshView(pak::geometry::MeshViewDesc {
                  .first_index = 0,
                  .index_count = index_count,
                  .first_vertex = 0,
                  .vertex_count = vertex_count,
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

auto BuildCubeGeometry(const char* geometry_name, const char* material_name,
  const glm::vec4& rgba, const float roughness = 0.9F,
  const float metalness = 0.0F,
  const oxygen::data::MaterialDomain domain
  = oxygen::data::MaterialDomain::kOpaque,
  const glm::vec3 emissive = glm::vec3 { 0.0F },
  const uint32_t extra_material_flags = 0U)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto cube_data = oxygen::data::MakeCubeMeshAsset();
  CHECK_F(cube_data.has_value());
  return BuildPrimitiveGeometry(geometry_name, material_name,
    std::move(cube_data->first), std::move(cube_data->second), rgba, roughness,
    metalness, domain, emissive, extra_material_flags);
}

auto BuildSphereGeometry(const char* geometry_name, const char* material_name,
  const glm::vec4& rgba, const float roughness,
  const oxygen::data::MaterialDomain domain, const glm::vec3 emissive,
  const uint32_t extra_material_flags = 0U)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto sphere_data = oxygen::data::MakeSphereMeshAsset(32U, 48U);
  CHECK_F(sphere_data.has_value());
  return BuildPrimitiveGeometry(geometry_name, material_name,
    std::move(sphere_data->first), std::move(sphere_data->second), rgba,
    roughness, 0.0F, domain, emissive, extra_material_flags);
}

auto BuildCylinderGeometry(const char* geometry_name, const char* material_name,
  const glm::vec4& rgba, const float roughness,
  const oxygen::data::MaterialDomain domain, const glm::vec3 emissive,
  const uint32_t extra_material_flags = 0U)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto cylinder_data = oxygen::data::MakeCylinderMeshAsset(32U, 1.0F, 0.5F);
  CHECK_F(cylinder_data.has_value());
  return BuildPrimitiveGeometry(geometry_name, material_name,
    std::move(cylinder_data->first), std::move(cylinder_data->second), rgba,
    roughness, 0.0F, domain, emissive, extra_material_flags);
}

} // namespace

namespace oxygen::examples::vortex_basic {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MainModule::MainModule(const DemoAppContext& app,
  const vortex::ShaderDebugMode shader_debug_mode,
  const ValidationOptions validation) noexcept
  : app_(app)
  , validation_(validation)
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

    platform::window::Properties props(validation_.sidedness_scene
        ? (validation_.shading_mode == vortex::ShadingMode::kDeferred
              ? "Oxygen Sidedness Validation - Deferred"
              : "Oxygen Sidedness Validation - Forward")
        : "Vortex Basic Example");
    constexpr uint32_t kWidth = 1920U;
    constexpr uint32_t kHeight = 1440U;
    props.extent = { .width = validation_.sidedness_scene ? 1600U : kWidth,
      .height = validation_.sidedness_scene ? 1000U : kHeight };
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
  if (validation_.IsExposureFixture()) {
    exposure_view_state_
      = vortex::CompositionView::ViewStateHandle { s_next_exposure_state_++ };
  }

  // Subscribe to Vortex Renderer attachment so we can hold an observer.
  renderer_subscription_ = engine->SubscribeModuleAttached(
    [this](const engine::ModuleEvent& event) {
      if (event.type_id != vortex::Renderer::ClassTypeId()) {
        return;
      }
      vortex_renderer_
        = observer_ptr { static_cast<vortex::Renderer*>(event.module.get()) };
      vortex_renderer_->SetGroundGridConfig(vortex::GroundGridConfig {
        .enabled = false,
      });
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
  translucent_sphere_node_ = {};
  translucent_cylinder_node_ = {};
  scene_.reset();
  vortex_renderer_.reset(nullptr);
  validation_normal_map_.reset();
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
      vortex_renderer_->SetGroundGridConfig(vortex::GroundGridConfig {
        .enabled = false,
      });
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

  if (validation_.normal_map) {
    if (!validation_normal_map_) {
      validation_normal_map_ = std::make_unique<NormalMapValidationTexture>(
        app_.engine->GetAssetLoader());
    }
    if (!validation_normal_map_->EnsureReady()) {
      co_return;
    }
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
    std::move(view_ctx), validation_.shading_mode, std::nullopt,
    exposure_view_state_);
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

  if (validation_.IsExposureFixture()) {
    BuildExposureScene();
    return;
  }
  if (validation_.sidedness_scene) {
    BuildSidednessScene();
    return;
  }

  constexpr size_t kCapacity = 32;
  scene_ = std::make_shared<scene::Scene>("VortexBasicScene", kCapacity);
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());

  if (const auto environment = scene_->GetEnvironment();
    environment != nullptr) {
    auto* atmosphere
      = environment->TryGetSystem<scene::environment::SkyAtmosphere>().get();
    if (atmosphere == nullptr) {
      atmosphere = &environment->AddSystem<scene::environment::SkyAtmosphere>();
    }
    atmosphere->SetEnabled(true);
    atmosphere->SetTransformMode(scene::environment::
        SkyAtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin);
    atmosphere->SetRenderInMainPass(true);
    atmosphere->SetPlanetRadiusMeters(engine::atmos::kDefaultPlanetRadiusM);
    atmosphere->SetAtmosphereHeightMeters(
      engine::atmos::kDefaultAtmosphereHeightM);
    atmosphere->SetGroundAlbedoRgb({ 0.4F, 0.4F, 0.4F });
    atmosphere->SetRayleighScatteringRgb(
      engine::atmos::kDefaultRayleighScatteringRgb);
    atmosphere->SetRayleighScaleHeightMeters(
      engine::atmos::kDefaultRayleighScaleHeightM);
    atmosphere->SetMieScatteringRgb(engine::atmos::kDefaultMieScatteringRgb);
    atmosphere->SetMieAbsorptionRgb(engine::atmos::kDefaultMieAbsorptionRgb);
    atmosphere->SetMieScaleHeightMeters(engine::atmos::kDefaultMieScaleHeightM);
    atmosphere->SetMieAnisotropy(engine::atmos::kDefaultMieAnisotropyG);
    atmosphere->SetOzoneAbsorptionRgb(
      engine::atmos::kDefaultOzoneAbsorptionRgb);
    atmosphere->SetOzoneDensityProfile(
      engine::atmos::kDefaultOzoneDensityProfile);
    atmosphere->SetMultiScatteringFactor(1.0F);
    atmosphere->SetSkyLuminanceFactorRgb({ 1.0F, 1.0F, 1.0F });
    atmosphere->SetSkyAndAerialPerspectiveLuminanceFactorRgb(
      { 1.0F, 1.0F, 1.0F });
    atmosphere->SetSunDiskEnabled(true);
    atmosphere->SetAerialPerspectiveDistanceScale(1.0F);
    atmosphere->SetAerialPerspectiveStartDepthMeters(
      (std::max)(app_.vortex_aerial_start_depth_m, 0.0F));
    atmosphere->SetAerialScatteringStrength(
      (std::max)(app_.vortex_aerial_scattering_strength, 0.0F));
    atmosphere->SetHeightFogContribution(1.0F);
    atmosphere->SetTraceSampleCountScale(1.0F);
    atmosphere->SetTransmittanceMinLightElevationDeg(-90.0F);

    auto* sky_light
      = environment->TryGetSystem<scene::environment::SkyLight>().get();
    if (sky_light == nullptr) {
      sky_light = &environment->AddSystem<scene::environment::SkyLight>();
    }
    sky_light->SetEnabled(true);
    sky_light->SetSource(scene::environment::SkyLightSource::kCapturedScene);
    sky_light->SetIntensityMul(1.0F);
    sky_light->SetTintRgb({ 1.0F, 1.0F, 1.0F });
    sky_light->SetDiffuseIntensity(1.0F);
    sky_light->SetSpecularIntensity(1.0F);
    sky_light->SetRealTimeCaptureEnabled(true);
    sky_light->SetLowerHemisphereColor({ 0.02F, 0.02F, 0.03F });
    sky_light->SetVolumetricScatteringIntensity(
      app_.vortex_sky_light_volumetric_scattering_intensity);
    sky_light->SetAffectReflections(true);

    auto* post_process
      = environment->TryGetSystem<scene::environment::PostProcessVolume>()
          .get();
    if (post_process == nullptr) {
      post_process
        = &environment->AddSystem<scene::environment::PostProcessVolume>();
    }
    post_process->SetExposureEnabled(true);
    post_process->SetExposureMode(engine::ExposureMode::kManual);
    post_process->SetManualExposureEv(13.0F);
    post_process->SetExposureCompensationEv(0.0F);
    post_process->SetExposureKey(engine::kExposureCalibrationKey);
    post_process->SetToneMapper(engine::ToneMapper::kAcesFitted);

    auto* fog = environment->TryGetSystem<scene::environment::Fog>().get();
    if (fog == nullptr) {
      fog = &environment->AddSystem<scene::environment::Fog>();
    }
    fog->SetEnabled(app_.with_height_fog || app_.with_volumetric_fog);
    fog->SetEnableHeightFog(app_.with_height_fog);
    fog->SetEnableVolumetricFog(app_.with_volumetric_fog);
    fog->SetRenderInMainPass(true);
    fog->SetVisibleInReflectionCaptures(true);
    fog->SetVisibleInRealTimeSkyCaptures(true);
    fog->SetExtinctionSigmaTPerMeter(0.0012F);
    fog->SetHeightFalloffPerMeter(0.12F);
    fog->SetHeightOffsetMeters(0.0F);
    fog->SetStartDistanceMeters(0.0F);
    fog->SetMaxOpacity(1.0F);
    fog->SetFogInscatteringLuminance({ 0.30F, 0.38F, 0.48F });
    fog->SetSkyAtmosphereAmbientContributionColorScale({ 1.0F, 1.0F, 1.0F });
    fog->SetDirectionalInscatteringLuminance({ 1.0F, 0.95F, 0.9F });
    fog->SetDirectionalInscatteringExponent(8.0F);
    fog->SetDirectionalInscatteringStartDistance(0.0F);
    fog->SetVolumetricFogScatteringDistribution(0.20F);
    fog->SetVolumetricFogAlbedo({ 0.62F, 0.70F, 0.82F });
    const auto volumetric_emissive_scale
      = std::max(app_.vortex_volumetric_fog_emissive_scale, 0.0F);
    fog->SetVolumetricFogEmissive({ 0.20F * volumetric_emissive_scale,
      0.26F * volumetric_emissive_scale, 0.32F * volumetric_emissive_scale });
    fog->SetVolumetricFogExtinctionScale(1.50F);
    fog->SetVolumetricFogDistance(120.0F);
    fog->SetVolumetricFogStartDistance(0.0F);
    fog->SetVolumetricFogNearFadeInDistance(8.0F);
    fog->SetVolumetricFogStaticLightingScatteringIntensity(1.0F);
  }

  auto cube_geo = BuildCubeGeometry(
    "ValidationCube", "ValidationCube", kCubeColor, 0.28F, 0.85F);
  auto occlusion_probe_geo = BuildCubeGeometry("OcclusionProbeCube",
    "OcclusionProbeCube", kOcclusionProbeColor, 0.5F, 0.0F);
  auto translucent_sphere_geo = BuildSphereGeometry("TranslucentCyanSphere",
    "TranslucentCyanSphere", kTranslucentSphereColor, 0.12F,
    oxygen::data::MaterialDomain::kAlphaBlended, glm::vec3 { 0.0F },
    oxygen::data::pak::render::kMaterialFlag_Unlit);
  auto translucent_cylinder_geo
    = BuildCylinderGeometry("TranslucentMagentaCylinder",
      "TranslucentMagentaCylinder", kTranslucentCylinderColor, 0.22F,
      oxygen::data::MaterialDomain::kAlphaBlended, glm::vec3 { 0.0F },
      oxygen::data::pak::render::kMaterialFlag_Unlit);
  auto floor_geo = BuildCubeGeometry(
    "ValidationFloor", "ValidationFloor", kFloorColor, 0.90F);

  cube_node_ = scene_->CreateNode("Cube");
  cube_node_.GetRenderable().SetGeometry(std::move(cube_geo));
  SetShadowParticipation(cube_node_, true, true);
  cube_node_.GetTransform().SetLocalPosition(kCubeCenter);
  cube_rotation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  cube_rotation_axis_ = RandomUnitAxis(cube_rotation_rng_);
  cube_node_.GetTransform().SetLocalRotation(cube_rotation_);

  if (app_.with_occlusion) {
    occlusion_probe_node_ = scene_->CreateNode("OcclusionProbe");
    occlusion_probe_node_.GetRenderable().SetGeometry(
      std::move(occlusion_probe_geo));
    SetShadowParticipation(occlusion_probe_node_, false, true);
    occlusion_probe_node_.GetTransform().SetLocalScale(kOcclusionProbeScale);
    occlusion_probe_node_.GetTransform().SetLocalPosition(
      kOcclusionProbeCenter);
  }

  if (app_.with_translucency) {
    translucent_sphere_node_ = scene_->CreateNode("TranslucentCyanSphere");
    translucent_sphere_node_.GetRenderable().SetGeometry(
      std::move(translucent_sphere_geo));
    SetShadowParticipation(translucent_sphere_node_, false, true);
    translucent_sphere_node_.GetTransform().SetLocalScale(
      kTranslucentSphereScale);
    translucent_sphere_node_.GetTransform().SetLocalPosition(
      kTranslucentSphereCenter);

    translucent_cylinder_node_
      = scene_->CreateNode("TranslucentMagentaCylinder");
    translucent_cylinder_node_.GetRenderable().SetGeometry(
      std::move(translucent_cylinder_geo));
    SetShadowParticipation(translucent_cylinder_node_, false, true);
    translucent_cylinder_node_.GetTransform().SetLocalScale(
      kTranslucentCylinderScale);
    translucent_cylinder_node_.GetTransform().SetLocalPosition(
      kTranslucentCylinderCenter);
    translucent_cylinder_node_.GetTransform().SetLocalRotation(
      glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  }

  floor_node_ = scene_->CreateNode("Floor");
  floor_node_.GetRenderable().SetGeometry(std::move(floor_geo));
  SetShadowParticipation(floor_node_, true, true);
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
      const auto local_fog_emissive_scale
        = std::max(app_.vortex_local_fog_emissive_scale, 0.0F);
      local_fog.SetFogEmissive({ 7.5F * local_fog_emissive_scale,
        0.4F * local_fog_emissive_scale, 5.5F * local_fog_emissive_scale });
      local_fog.SetSortPriority(2);
    }
    local_fog_volume_node_.GetTransform().SetLocalPosition(
      { 0.0F, 0.0F, 1.6F });
    local_fog_volume_node_.GetTransform().SetLocalScale({ 8.0F, 8.0F, 8.0F });
  }
}

auto MainModule::BuildSidednessScene() -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = d::pak;

  scene_ = std::make_shared<scene::Scene>("SidednessValidation", 128);
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene_->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  post.SetExposureEnabled(true);
  post.SetExposureMode(engine::ExposureMode::kManual);
  post.SetManualExposureEv(13.0F);
  post.SetExposureCompensationEv(0.0F);
  post.SetExposureKey(engine::kExposureCalibrationKey);
  post.SetToneMapper(engine::ToneMapper::kAcesFitted);

  validation_root_ = scene_->CreateNode("SidednessChart");
  const auto make_child
    = [this](scene::SceneNode& parent, const std::string& name) {
        auto node = scene_->CreateChildNode(parent, name);
        CHECK_F(node.has_value(), "Failed to create validation node {}", name);
        return *node;
      };
  const auto vertices = std::vector<Vertex> {
    { .position = { -0.75F, 0.0F, -0.7F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F } },
    { .position = { 0.75F, 0.0F, -0.7F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 1.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F } },
    { .position = { -0.35F, 0.0F, 0.8F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F } },
  };
  // Asymmetric silhouette makes the mirror observable. Every non-double-sided
  // column in a row shares the same geometry, including opposite determinants.
  constexpr auto columns = std::array {
    "SingleFront",
    "SingleBack",
    "DoubleFront",
    "DoubleBack",
    "MirrorSingleFront",
    "MirrorSingleBack",
    "MirrorDoubleFront",
    "MirrorDoubleBack",
    "ParentMirrorFront",
    "TwoMirrorsFront",
  };
  constexpr auto domains = std::array { d::MaterialDomain::kOpaque,
    d::MaterialDomain::kMasked, d::MaterialDomain::kAlphaBlended };
  constexpr auto rows = std::array { "Opaque", "Masked", "Translucent" };
  constexpr auto colors
    = std::array { glm::vec4 { 0.82F, 0.055F, 0.025F, 1.0F },
        glm::vec4 { 0.10F, 0.74F, 0.045F, 1.0F },
        glm::vec4 { 0.015F, 0.5F, 0.9F, 0.55F } };
  auto panel_geometry = BuildCubeGeometry(
    "SidednessPanel", "SidednessPanel", { 0.20F, 0.22F, 0.25F, 1.0F });
  const auto normal_map = validation_normal_map_ ? validation_normal_map_->Key()
                                                 : content::ResourceKey {};

  for (size_t row = 0; row < rows.size(); ++row) {
    const auto flags = row == 1 ? pak::render::kMaterialFlag_AlphaTest : 0U;
    const auto single_name = std::string(rows[row]) + "Single";
    const auto double_name = std::string(rows[row]) + "Double";
    auto single_geometry = BuildPrimitiveGeometry(single_name.c_str(),
      single_name.c_str(), vertices, { 0U, 1U, 2U }, colors[row], 0.75F, 0.0F,
      domains[row], glm::vec3 { 0.0F }, flags, false, normal_map);
    auto double_geometry = BuildPrimitiveGeometry(double_name.c_str(),
      double_name.c_str(), vertices, { 0U, 1U, 2U }, colors[row], 0.75F, 0.0F,
      domains[row], glm::vec3 { 0.0F }, flags, true, normal_map);

    for (size_t column = 0; column < columns.size(); ++column) {
      const auto name = std::string(rows[row]) + "_" + columns[column];
      const auto position
        = glm::vec3 { (static_cast<float>(column) - 4.5F) * 2.3F, 0.0F,
            3.8F - static_cast<float>(row) * 2.7F };
      auto parent = validation_root_;
      if (column >= 8) {
        parent = make_child(validation_root_, name + "_Parent");
        parent.GetTransform().SetLocalPosition(position);
        parent.GetTransform().SetLocalScale({ -1.0F, 1.0F, 1.0F });
      }
      auto node = make_child(parent, name);
      node.GetRenderable().SetGeometry(
        column == 2 || column == 3 || column == 6 || column == 7
          ? double_geometry
          : single_geometry);
      node.GetTransform().SetLocalPosition(
        column >= 8 ? glm::vec3 { 0.0F } : position);
      if ((column >= 4 && column <= 7) || column == 9) {
        node.GetTransform().SetLocalScale({ -1.0F, 1.0F, 1.0F });
      }
      if (column == 1 || column == 3 || column == 5 || column == 7) {
        node.GetTransform().SetLocalRotation(
          glm::angleAxis(math::Pi, glm::vec3 { 0.0F, 0.0F, 1.0F }));
      }
      SetShadowParticipation(node, true, true);

      auto panel = make_child(validation_root_, name + "_Receiver");
      panel.GetRenderable().SetGeometry(panel_geometry);
      panel.GetTransform().SetLocalPosition(
        position + glm::vec3 { 0.0F, 0.9F, 0.0F });
      panel.GetTransform().SetLocalScale({ 2.05F, 0.1F, 2.25F });
      SetShadowParticipation(panel, false, true);
    }
  }

  // Positive and negative determinant instances of closed meshes provide a
  // familiar exterior/normal regression check beside the triangle chart.
  auto cube_data = d::MakeCubeMeshAsset();
  auto sphere_data = d::MakeSphereMeshAsset(24U, 32U);
  CHECK_F(cube_data.has_value() && sphere_data.has_value());
  auto cube = BuildPrimitiveGeometry("SidednessCube", "SidednessControls",
    std::move(cube_data->first), std::move(cube_data->second),
    { 0.8F, 0.42F, 0.025F, 1.0F }, 0.55F, 0.0F, d::MaterialDomain::kOpaque,
    glm::vec3 { 0.0F }, 0U, false);
  auto sphere = BuildPrimitiveGeometry("SidednessSphere", "SidednessControls",
    std::move(sphere_data->first), std::move(sphere_data->second),
    { 0.8F, 0.42F, 0.025F, 1.0F }, 0.55F, 0.0F, d::MaterialDomain::kOpaque,
    glm::vec3 { 0.0F }, 0U, false);
  for (size_t index = 0; index < 4; ++index) {
    const auto mirrored = index % 2 != 0;
    auto node = make_child(validation_root_,
      std::string(index < 2 ? "Cube" : "Sphere")
        + (mirrored ? "Mirrored" : "Normal"));
    node.GetRenderable().SetGeometry(index < 2 ? cube : sphere);
    node.GetTransform().SetLocalPosition(
      { (static_cast<float>(index) - 1.5F) * 3.1F, 0.0F, -4.6F });
    node.GetTransform().SetLocalScale(
      { mirrored ? -1.25F : 1.25F, 1.25F, 1.25F });
    node.GetTransform().SetLocalRotation(
      glm::angleAxis(0.25F, glm::vec3 { 0.0F, 0.0F, 1.0F }));
    SetShadowParticipation(node, true, true);
  }

  directional_light_node_ = scene_->CreateNode("ValidationKeyLight");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->Common().affects_world = true;
  light->Common().casts_shadows = true;
  light->Common().shadow.bias = kDefaultDemoSunShadowBias;
  light->Common().color_rgb = { 1.0F, 1.0F, 1.0F };
  light->SetIntensityLux(100000.0F);
    light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetUsePerPixelAtmosphereTransmittance(false);
  CHECK_F(directional_light_node_.AttachLight(std::move(light)));
  directional_light_node_.GetTransform().SetLocalRotation(
    LookRotation({ -3.0F, -10.0F, 6.0F }, glm::vec3 { 0.0F }));

  LOG_F(INFO,
    "Sidedness chart rows: opaque red, masked green, translucent blue; "
    "columns: single front/back, double front/back, mirrored single "
    "front/back, "
    "mirrored double front/back, inherited mirror front, two mirrors front. "
    "Columns 2 and 6 must show only the gray receiver in every row. "
    "All other triangles must be lit. Bottom controls: cube, mirrored cube, "
    "sphere, mirrored sphere.");
  // NOLINTEND(*-magic-numbers)
}

auto MainModule::BuildExposureScene() -> void
{
  scene_ = std::make_shared<scene::Scene>("FixedExposureFixture", 8U);
  scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene_->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  post.SetExposureEnabled(validation_.exposure_enabled);
  post.SetExposureMode(validation_.camera_exposure
      ? engine::ExposureMode::kManualCamera
      : engine::ExposureMode::kManual);
  post.SetManualExposureEv(validation_.fixed_exposure_ev);
  post.SetExposureKey(validation_.exposure_key);
  post.SetExposureCompensationEv(validation_.exposure_compensation);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);

  const auto fixture = validation_.exposure_fixture;
  const bool automatic = fixture != ValidationOptions::ExposureFixture::kFixed;
  if (automatic) {
    auto settings = post.GetExposureSettings();
    settings.mode = engine::ExposureMode::kAuto;
    settings.manual_ev = 9.7F;
    if (fixture == ValidationOptions::ExposureFixture::kLockedAuto) {
      settings.min_ev = settings.max_ev = validation_.fixed_exposure_ev;
      settings.compensation_ev = validation_.fixed_exposure_ev;
    } else {
      settings.key = 25.0F;
      settings.compensation_ev = 1.0e20F;
      settings.compensation_curve = { { 0.0F, -1.0e20F } };
      // Lock the arithmetic fixture so asset-readiness startup frames cannot
      // turn this cancellation check into a temporal-settling experiment.
      settings.min_ev = settings.max_ev = 0.0F;
    }
    post.SetExposureSettings(settings);
  }

  // Exact binary16 input, no lights/environment. The front face fills the
  // camera so interior probes avoid silhouette and partial coverage.
  const float input = automatic ? 0.25F : 4096.0F;
  auto receiver = scene_->CreateNode("ExposureReceiver");
  receiver.GetRenderable().SetGeometry(BuildCubeGeometry("ExposureReceiver",
    "ExposureReceiver", glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F }, 1.0F, 0.0F,
    data::MaterialDomain::kOpaque, glm::vec3 { input }));
  receiver.GetTransform().SetLocalScale({ 100.0F, 1.0F, 100.0F });
  SetShadowParticipation(receiver, false, false);
  LOG_F(INFO, "Exposure fixture: input={} EV={} auto={} gamma=1 None", input,
    validation_.fixed_exposure_ev, automatic);
}

auto MainModule::EnsureLighting() -> void
{
  if (!scene_ || validation_.sidedness_scene
    || validation_.IsExposureFixture()) {
    return;
  }

  if (!directional_light_node_.IsAlive()) {
    directional_light_node_ = scene_->CreateNode("SunLight");
    auto light = std::make_unique<scene::DirectionalLight>();
    light->Common().affects_world = true;
    light->Common().casts_shadows = true;
    light->Common().shadow.bias = kDefaultDemoSunShadowBias;
    light->Common().color_rgb = { 1.0F, 0.97F, 0.92F };
    light->SetAngularSizeRadians(glm::radians(0.53F));
    light->SetIntensityLux(100000.0F);
    light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    light->SetUsePerPixelAtmosphereTransmittance(true);
    light->SetAtmosphereDiskLuminanceScale({ 1.0F, 0.95F, 0.9F });
    CHECK_F(directional_light_node_.AttachLight(std::move(light)),
      "Failed to attach DirectionalLight to SunLight");
  }
  if (!point_light_node_.IsAlive()) {
    point_light_node_ = scene_->CreateNode("PointFillLight");
    auto light = std::make_unique<scene::PointLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 0.35F, 0.60F, 1.0F };
    light->SetRange(8.0F);
    light->SetLuminousFluxLm(1800.0F);
    CHECK_F(point_light_node_.AttachLight(std::move(light)),
      "Failed to attach PointLight to PointFillLight");
  }
  if (!spot_light_node_.IsAlive()) {
    spot_light_node_ = scene_->CreateNode("SpotRimLight");
    auto light = std::make_unique<scene::SpotLight>();
    light->Common().affects_world = true;
    light->Common().color_rgb = { 1.0F, 0.58F, 0.24F };
    light->SetRange(10.0F);
    light->SetLuminousFluxLm(1400.0F);
    light->SetInnerConeAngleRadians(0.35F);
    light->SetOuterConeAngleRadians(0.70F);
    CHECK_F(spot_light_node_.AttachLight(std::move(light)),
      "Failed to attach SpotLight to SpotRimLight");
  }

  directional_light_node_.GetTransform().SetLocalPosition(kSunPosition);
  directional_light_node_.GetTransform().SetLocalRotation(
    LookRotation(kSunPosition, kSceneFocusPoint));
  point_light_node_.GetTransform().SetLocalPosition(
    kSceneFocusPoint + glm::vec3 { kPointLightOrbitRadius, 0.0F, 1.2F });
  const auto initial_spot_position
    = kSceneFocusPoint + glm::vec3 { -3.0F, -5.0F, 4.0F };
  spot_light_node_.GetTransform().SetLocalPosition(initial_spot_position);
  spot_light_node_.GetTransform().SetLocalRotation(
    LookRotation(initial_spot_position, kSceneFocusPoint));
}

auto MainModule::UpdateValidationScene(
  const observer_ptr<engine::FrameContext> context) -> void
{
  if (validation_.IsExposureFixture()) {
    if (validation_.inject_invalid_exposure && context != nullptr
      && context->GetFrameSequenceNumber().get() >= 8U) {
      scene_->GetEnvironment()
        ->TryGetSystem<scene::environment::PostProcessVolume>()
        ->SetExposureKey(-1.0F);
    }
    return;
  }
  if (validation_.sidedness_scene) {
    if (validation_.animate && context != nullptr) {
      const auto frame
        = static_cast<float>(context->GetFrameSequenceNumber().get());
      validation_root_.GetTransform().SetLocalPosition(
        { 0.25F * std::sin(frame * 0.05F), 0.0F, 0.0F });
    }
    return;
  }
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
  if (occlusion_probe_node_.IsAlive()) {
    occlusion_probe_node_.GetTransform().SetLocalScale(kOcclusionProbeScale);
    occlusion_probe_node_.GetTransform().SetLocalPosition(
      kOcclusionProbeCenter);
  }
  if (translucent_sphere_node_.IsAlive()) {
    translucent_sphere_node_.GetTransform().SetLocalScale(
      kTranslucentSphereScale);
    translucent_sphere_node_.GetTransform().SetLocalPosition(
      kTranslucentSphereCenter);
  }
  if (translucent_cylinder_node_.IsAlive()) {
    translucent_cylinder_node_.GetTransform().SetLocalScale(
      kTranslucentCylinderScale);
    translucent_cylinder_node_.GetTransform().SetLocalPosition(
      kTranslucentCylinderCenter);
    translucent_cylinder_node_.GetTransform().SetLocalRotation(
      glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
  }

  if (directional_light_node_.IsAlive()) {
    directional_light_node_.GetTransform().SetLocalPosition(kSunPosition);
    directional_light_node_.GetTransform().SetLocalRotation(
      LookRotation(kSunPosition, kSceneFocusPoint));
  }
  if (point_light_node_.IsAlive()) {
    const float point_phase
      = (animation_time_seconds_ / kPointLightOrbitPeriodSeconds)
      * oxygen::math::TwoPi;
    const auto offset
      = glm::vec3 { std::cos(point_phase), std::sin(point_phase), 0.65F }
      * kPointLightOrbitRadius;
    point_light_node_.GetTransform().SetLocalPosition(
      kSceneFocusPoint + offset);
  }
  if (spot_light_node_.IsAlive()) {
    const float spot_phase
      = (animation_time_seconds_ / kSpotlightOscillationPeriodSeconds)
      * oxygen::math::TwoPi;
    const auto spot_position = kSceneFocusPoint
      + glm::vec3 { -3.0F,
          -5.0F + std::sin(spot_phase) * kSpotlightOscillationAmplitude, 4.0F };
    spot_light_node_.GetTransform().SetLocalPosition(spot_position);
    spot_light_node_.GetTransform().SetLocalRotation(
      LookRotation(spot_position, kSceneFocusPoint));
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

  const bool chart_camera
    = validation_.sidedness_scene || validation_.IsExposureFixture();
  const glm::vec3 kCameraPosition = chart_camera
    ? glm::vec3 { 0.0F, -17.0F, 0.0F }
    : glm::vec3 { 0.0F, 8.0F, 4.0F };
  const glm::vec3 kCameraTarget
    = chart_camera ? glm::vec3 { 0.0F } : glm::vec3 { 0.0F, 0.0F, 1.5F };
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
