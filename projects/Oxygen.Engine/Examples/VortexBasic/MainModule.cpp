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
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "VortexBasic/MainModule.h"
#include "VortexBasic/NormalMapValidationTexture.h"
#include <fmt/format.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/OxCo/Co.h>
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
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>
#include <Oxygen/Vortex/Types/GroundGridConfig.h>

using oxygen::ViewPort;
using oxygen::data::Vertex;
using oxygen::scene::PerspectiveCamera;

namespace {

constexpr uint32_t kDefaultOffscreenWidth = 1920U;
constexpr uint32_t kDefaultOffscreenHeight = 1440U;
constexpr glm::vec3 kFloorCenter { 0.0F, 0.0F, -0.125F };
constexpr glm::vec3 kFloorScale { 400.0F, 400.0F, 0.25F };
constexpr glm::vec4 kFloorColor { 0.02F, 0.02F, 0.03F, 1.0F };
constexpr glm::vec3 kCubeCenter { 0.0F, 0.0F, 2.0F };
constexpr glm::vec3 kOcclusionProbeCenter { 0.0F, -1.45F, 1.55F };
constexpr glm::vec3 kOcclusionProbeScale { 0.45F, 0.45F, 0.45F };
constexpr float kOcclusionProbeSwing = 1.6F;
constexpr float kOcclusionProbePeriodSeconds = 6.0F;
constexpr glm::vec3 kTranslucentSphereCenter { 2.60F, 1.80F, 1.10F };
constexpr glm::vec3 kTranslucentCylinderCenter { -2.60F, 2.20F, 1.05F };
constexpr glm::vec3 kTranslucentSphereScale { 0.85F, 0.85F, 0.85F };
constexpr glm::vec3 kTranslucentCylinderScale { 0.48F, 0.48F, 1.05F };
constexpr glm::vec4 kCubeColor { 0.82F, 0.80F, 0.74F, 1.0F };
constexpr glm::vec4 kOcclusionProbeColor { 1.0F, 0.12F, 0.08F, 1.0F };
constexpr glm::vec4 kTranslucentSphereColor { 0.00F, 0.95F, 1.0F, 0.45F };
constexpr glm::vec4 kTranslucentCylinderColor { 1.0F, 0.10F, 0.82F, 0.45F };
constexpr glm::vec3 kSceneFocusPoint { 0.0F, 0.0F, 1.0F };
//! Matches the bias DemoShell gives the suns it creates.
constexpr float kSunShadowBias = 0.03F;
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

//! Rotation that turns the forward axis toward `target`.
auto LookRotation(const glm::vec3& position, const glm::vec3& target)
  -> glm::quat
{
  const auto& from_dir = oxygen::space::move::Forward;
  const auto direction = target - position;
  const auto length_sq = glm::dot(direction, direction);
  const auto to_dir = length_sq <= oxygen::math::Epsilon
    ? from_dir
    : direction / std::sqrt(length_sq);
  const auto cos_theta = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);

  if (cos_theta >= 0.9999F) {
    return { 1.0F, 0.0F, 0.0F, 0.0F };
  }
  if (cos_theta <= -0.9999F) {
    return glm::angleAxis(oxygen::math::Pi, oxygen::space::move::Up);
  }
  const auto axis = glm::normalize(glm::cross(from_dir, to_dir));
  return glm::angleAxis(std::acos(cos_theta), axis);
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

auto CameraLookRotation(const glm::vec3& position, const glm::vec3& target)
  -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return { 1.0F, 0.0F, 0.0F, 0.0F };
  }

  const auto forward = glm::normalize(forward_raw);
  glm::vec3 up_dir = oxygen::space::move::Up;
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

  const glm::mat4 look_matrix { glm::vec4(right, 0.0F), glm::vec4(up, 0.0F),
    glm::vec4(-forward, 0.0F), glm::vec4(0.0F, 0.0F, 0.0F, 1.0F) };
  return glm::quat_cast(look_matrix);
}

//! A solid-colour material without textures, unless it has a normal map.
struct MaterialSpec {
  //! The material's name; empty uses the geometry's name.
  std::string_view name;
  glm::vec4 rgba { 1.0F };
  float roughness { 0.9F };
  float metalness { 0.0F };
  oxygen::data::MaterialDomain domain { oxygen::data::MaterialDomain::kOpaque };
  glm::vec3 emissive { 0.0F };
  uint32_t extra_flags { 0U };
  bool double_sided { true };
  oxygen::content::ResourceKey normal_map {};
};

auto MakeSolidColorMaterial(
  const std::string_view name, const MaterialSpec& spec)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::render::MaterialAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(d::AssetType::kMaterial);
  constexpr std::size_t kMaxName = sizeof(desc.header.name) - 1;
  const auto stored = name.substr(0, (std::min)(kMaxName, name.size()));
  std::ranges::copy(stored, std::data(desc.header.name));
  desc.header.name[stored.size()] = '\0';
  desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(spec.domain);
  desc.flags = pak::render::kMaterialFlag_NoTextureSampling | spec.extra_flags;
  if (spec.double_sided) {
    desc.flags |= pak::render::kMaterialFlag_DoubleSided;
  }
  if (spec.normal_map != oxygen::content::ResourceKey {}) {
    desc.flags &= ~pak::render::kMaterialFlag_NoTextureSampling;
  }
  desc.shader_stages = 0;
  desc.base_color[0] = spec.rgba.r;
  desc.base_color[1] = spec.rgba.g;
  desc.base_color[2] = spec.rgba.b;
  desc.base_color[3] = spec.rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { spec.metalness };
  desc.roughness = d::Unorm16 { spec.roughness };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  desc.emissive_factor[0] = spec.emissive.r;
  desc.emissive_factor[1] = spec.emissive.g;
  desc.emissive_factor[2] = spec.emissive.b;
  const auto key = d::AssetKey::FromVirtualPath(
    "/Engine/Examples/VortexBasic/Materials/" + std::string(name) + ".omat");
  return std::make_shared<const d::MaterialAsset>(key, desc,
    std::vector<d::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { {}, spec.normal_map });
  // NOLINTEND(*-magic-numbers)
}

//! A single-mesh geometry named `name` with one material.
auto BuildPrimitiveGeometry(const std::string_view name,
  std::vector<Vertex> vertices, std::vector<uint32_t> indices,
  const MaterialSpec& material_spec)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  namespace d = oxygen::data;
  namespace pak = d::pak;

  const auto material = MakeSolidColorMaterial(
    material_spec.name.empty() ? name : material_spec.name, material_spec);
  const auto vertex_count = static_cast<uint32_t>(vertices.size());
  const auto index_count = static_cast<uint32_t>(indices.size());
  auto mesh = d::MeshBuilder(oxygen::data::LodIndex {}, name)
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
    d::AssetKey::FromVirtualPath(
      "/Engine/Examples/VortexBasic/Geometry/" + std::string(name) + ".ogeo"),
    geo_desc, std::vector<std::shared_ptr<d::Mesh>> { std::move(mesh) });
}

auto BuildCubeGeometry(const std::string_view name,
  const MaterialSpec& material) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto cube_data = oxygen::data::MakeCubeMeshAsset();
  CHECK_F(cube_data.has_value());
  return BuildPrimitiveGeometry(
    name, std::move(cube_data->first), std::move(cube_data->second), material);
}

auto BuildSphereGeometry(const std::string_view name,
  const MaterialSpec& material) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto sphere_data = oxygen::data::MakeSphereMeshAsset(32U, 48U);
  CHECK_F(sphere_data.has_value());
  return BuildPrimitiveGeometry(name, std::move(sphere_data->first),
    std::move(sphere_data->second), material);
}

auto BuildCylinderGeometry(const std::string_view name,
  const MaterialSpec& material) -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  auto cylinder_data = oxygen::data::MakeCylinderMeshAsset(32U, 1.0F, 0.5F);
  CHECK_F(cylinder_data.has_value());
  return BuildPrimitiveGeometry(name, std::move(cylinder_data->first),
    std::move(cylinder_data->second), material);
}

} // namespace

namespace oxygen::examples::vortex_basic {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MainModule::MainModule(const host::HostContext& host,
  const vortex::ShaderDebugMode shader_debug_mode,
  const ValidationOptions validation, const SceneOptions scene_options)
  : HostModule(host)
  , validation_(validation)
  , scene_options_(scene_options)
  , shader_debug_mode_(shader_debug_mode)
{
}

MainModule::~MainModule()
{
  renderer_subscription_.Cancel();
  scene_.reset();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  constexpr uint32_t kWidth = 1920U;
  constexpr uint32_t kHeight = 1440U;
  constexpr uint32_t kChartWidth = 1600U;
  constexpr uint32_t kChartHeight = 1000U;
  auto props = HostModule::BuildWindowProperties();
  if (!validation_.sidedness_scene) {
    props.title = "Vortex Basic Example";
    props.extent = { .width = kWidth, .height = kHeight };
  } else {
    props.title = validation_.shading_mode == vortex::ShadingMode::kDeferred
      ? "Oxygen Sidedness Validation - Deferred"
      : "Oxygen Sidedness Validation - Forward";
    props.extent = { .width = kChartWidth, .height = kChartHeight };
  }
  return props;
}

auto MainModule::OnHostAttached(observer_ptr<IAsyncEngine> engine) -> bool
{
  DCHECK_NOTNULL_F(engine);
  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  // Temporal view state: without it Vortex skips the atmosphere LUT and
  // aerial-perspective history and volumetric fog, and exposure has no
  // history to adapt from.
  main_view_state_
    = vortex::CompositionView::ViewStateHandle { s_next_view_state_++ };

  renderer_subscription_ = engine->SubscribeModuleAttached(
    [this](const engine::ModuleEvent& event) -> void {
      if (event.type_id != vortex::Renderer::ClassTypeId()) {
        return;
      }
      auto& renderer = static_cast<vortex::Renderer&>(*event.module);
      renderer.SetGroundGridConfig(vortex::GroundGridConfig {
        .enabled = scene_options_.with_grid,
      });
      renderer.SetShaderDebugMode(shader_debug_mode_);
    },
    true);
  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  renderer_subscription_.Cancel();
  if (const auto renderer = ResolveRenderer()) {
    renderer->SetShaderDebugMode(vortex::ShaderDebugMode::kDisabled);
  }
  camera_node_ = {};
  cube_node_ = {};
  translucent_sphere_node_ = {};
  translucent_cylinder_node_ = {};
  scene_.reset();
  validation_normal_map_.reset();
  HostModule::OnShutdown();
}

// ---------------------------------------------------------------------------
// Frame phases
// ---------------------------------------------------------------------------

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (HasRenderableWindow()) {
    bool ready = true;
    if (validation_.normal_map) {
      if (!validation_normal_map_) {
        validation_normal_map_ = std::make_unique<NormalMapValidationTexture>(
          Host().engine->GetAssetLoader());
      }
      ready = validation_normal_map_->EnsureReady();
    }
    if (ready) {
      EnsureScene();
      EnsureLighting();
      const auto extent = ResolveViewExtent();
      EnsureCamera(extent.x, extent.y);
      UpdateValidationScene(context);
      if (scene_) {
        scene_->Update(false);
      }
    }
  }
  co_await HostModule::OnSceneMutation(context);
}

auto MainModule::BuildViews(
  engine::FrameContext& /*context*/, host::HostViews& views) -> void
{
  if (!camera_node_.IsAlive()) {
    return;
  }
  const auto extent = ResolveViewExtent();
  if (extent.x == 0 || extent.y == 0) {
    return;
  }

  View view {};
  view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.x),
    .height = static_cast<float>(extent.y),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  auto main_view
    = vortex::CompositionView::ForScene(main_view_id_, view, camera_node_);
  main_view.name = "MainView";
  main_view.view_state_handle = main_view_state_;
  main_view.with_atmosphere = scene_options_.with_atmosphere;
  main_view.with_height_fog = scene_options_.with_height_fog;
  main_view.with_local_fog = scene_options_.with_local_fog;
  main_view.shading_mode = validation_.shading_mode;
  views.views.push_back(std::move(main_view));
  views.main = main_view_id_;
}

// ---------------------------------------------------------------------------
// Scene setup
// ---------------------------------------------------------------------------

auto MainModule::CreateScene(const char* name, const std::size_t capacity)
  -> void
{
  // Staging collects the build's mutations; the host publishes the scene at
  // the next frame start.
  scene_ = std::make_shared<scene::Scene>(name, capacity);
  Stage().Stage(scene_);
}

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
  CreateScene("VortexBasicScene", kCapacity);
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
      (std::max)(scene_options_.aerial_start_depth_m, 0.0F));
    atmosphere->SetAerialScatteringStrength(
      (std::max)(scene_options_.aerial_scattering_strength, 0.0F));
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
    sky_light->SetLowerHemisphereColor({ 0.02F, 0.02F, 0.03F });
    sky_light->SetVolumetricScatteringIntensity(
      scene_options_.sky_light_volumetric_scattering_intensity);
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
    fog->SetEnabled(
      scene_options_.with_height_fog || scene_options_.with_volumetric_fog);
    fog->SetEnableHeightFog(scene_options_.with_height_fog);
    fog->SetEnableVolumetricFog(scene_options_.with_volumetric_fog);
    fog->SetRenderInMainPass(true);
    fog->SetVisibleInReflectionCaptures(true);
    fog->SetVisibleInRealTimeSkyCaptures(true);
    fog->SetExtinctionSigmaTPerMeter(0.02F);
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
      = std::max(scene_options_.volumetric_fog_emissive_scale, 0.0F);
    fog->SetVolumetricFogEmissive({ 0.20F * volumetric_emissive_scale,
      0.26F * volumetric_emissive_scale, 0.32F * volumetric_emissive_scale });
    fog->SetVolumetricFogExtinctionScale(1.50F);
    fog->SetVolumetricFogDistance(120.0F);
    fog->SetVolumetricFogStartDistance(0.0F);
    fog->SetVolumetricFogNearFadeInDistance(8.0F);
    fog->SetVolumetricFogStaticLightingScatteringIntensity(1.0F);
  }

  auto cube_geo = BuildCubeGeometry("ValidationCube",
    { .rgba = kCubeColor, .roughness = 0.28F, .metalness = 0.85F });
  auto occlusion_probe_geo = BuildCubeGeometry(
    "OcclusionProbeCube", { .rgba = kOcclusionProbeColor, .roughness = 0.5F });
  auto translucent_sphere_geo = BuildSphereGeometry("TranslucentCyanSphere",
    {
      .rgba = kTranslucentSphereColor,
      .roughness = 0.12F,
      .domain = oxygen::data::MaterialDomain::kAlphaBlended,
    });
  auto translucent_cylinder_geo
    = BuildCylinderGeometry("TranslucentMagentaCylinder",
      {
        .rgba = kTranslucentCylinderColor,
        .roughness = 0.22F,
        .domain = oxygen::data::MaterialDomain::kAlphaBlended,
      });
  auto floor_geo = BuildCubeGeometry(
    "ValidationFloor", { .rgba = kFloorColor, .roughness = 0.90F });

  cube_node_ = scene_->CreateNode("Cube");
  cube_node_.GetRenderable().SetGeometry(std::move(cube_geo));
  SetShadowParticipation(cube_node_, true, true);
  cube_node_.GetTransform().SetLocalPosition(kCubeCenter);
  cube_rotation_ = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  cube_rotation_axis_ = RandomUnitAxis(cube_rotation_rng_);
  cube_node_.GetTransform().SetLocalRotation(cube_rotation_);

  if (scene_options_.with_occlusion) {
    occlusion_probe_node_ = scene_->CreateNode("OcclusionProbe");
    occlusion_probe_node_.GetRenderable().SetGeometry(
      std::move(occlusion_probe_geo));
    SetShadowParticipation(occlusion_probe_node_, false, true);
    occlusion_probe_node_.GetTransform().SetLocalScale(kOcclusionProbeScale);
    occlusion_probe_node_.GetTransform().SetLocalPosition(
      kOcclusionProbeCenter);
  }

  if (scene_options_.with_translucency) {
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

  if (scene_options_.with_contact_shadows) {
    AddContactShadowPebbles();
  }

  if (scene_options_.with_local_fog) {
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
        = std::max(scene_options_.local_fog_emissive_scale, 0.0F);
      local_fog.SetFogEmissive({ 7.5F * local_fog_emissive_scale,
        0.4F * local_fog_emissive_scale, 5.5F * local_fog_emissive_scale });
      local_fog.SetSortPriority(2);
    }
    // The volume's base radius is 5 m; scaled to 2.5 m beside the cube so
    // the camera stays outside it.
    local_fog_volume_node_.GetTransform().SetLocalPosition(
      { -3.0F, -1.0F, 1.5F });
    local_fog_volume_node_.GetTransform().SetLocalScale({ 0.5F, 0.5F, 0.5F });
  }
}

//! Small blocks and a thin post on the floor in front of the cube. They are
//! below the shadow maps' resolution, so only contact shadows ground them.
auto MainModule::AddContactShadowPebbles() -> void
{
  struct Pebble {
    glm::vec3 position;
    glm::vec3 scale;
  };
  // Positions are the base centre on the floor (z = 0).
  constexpr auto kPebbles = std::array {
    Pebble {
      .position = { -1.20F, 3.20F, 0.0F },
      .scale = { 0.12F, 0.12F, 0.12F },
    },
    Pebble {
      .position = { -0.60F, 3.40F, 0.0F },
      .scale = { 0.20F, 0.20F, 0.06F },
    },
    Pebble {
      .position = { 0.00F, 3.20F, 0.0F },
      .scale = { 0.06F, 0.06F, 0.25F },
    },
    Pebble {
      .position = { 0.60F, 3.40F, 0.0F },
      .scale = { 0.25F, 0.15F, 0.08F },
    },
    Pebble {
      .position = { 1.20F, 3.20F, 0.0F },
      .scale = { 0.04F, 0.04F, 0.80F },
    },
  };
  constexpr glm::vec4 kPebbleColor { 0.75F, 0.72F, 0.66F, 1.0F };
  const auto geometry = BuildCubeGeometry(
    "ContactShadowPebble", { .rgba = kPebbleColor, .roughness = 0.6F });
  for (std::size_t i = 0; i < kPebbles.size(); ++i) {
    const auto& pebble = kPebbles.at(i);
    auto node = scene_->CreateNode(fmt::format("ContactShadowPebble{}", i));
    node.GetRenderable().SetGeometry(geometry);
    SetShadowParticipation(node, true, true);
    node.GetTransform().SetLocalScale(pebble.scale);
    node.GetTransform().SetLocalPosition(
      pebble.position + glm::vec3 { 0.0F, 0.0F, pebble.scale.z * 0.5F });
  }
}

auto MainModule::BuildSidednessScene() -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = d::pak;

  CreateScene("SidednessValidation", 128);
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
    = [this](scene::SceneNode& parent,
        const std::string& name) -> oxygen::scene::SceneNode {
    auto node = scene_->CreateChildNode(parent, name);
    CHECK_F(node.has_value(), "Failed to create validation node {}", name);
    return *node;
  };
  const auto vertices = std::vector<Vertex> {
    {
      .position = { -0.75F, 0.0F, -0.7F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    {
      .position = { 0.75F, 0.0F, -0.7F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 1.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    },
    {
      .position = { -0.35F, 0.0F, 0.8F },
      .normal = { 0.0F, -1.0F, 0.0F },
      .texcoord = { 0.0F, 1.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 0.0F, 1.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    },
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
  constexpr auto domains = std::array {
    d::MaterialDomain::kOpaque,
    d::MaterialDomain::kMasked,
    d::MaterialDomain::kAlphaBlended,
  };
  constexpr auto rows = std::array { "Opaque", "Masked", "Translucent" };
  constexpr auto colors = std::array {
    glm::vec4 { 0.82F, 0.055F, 0.025F, 1.0F },
    glm::vec4 { 0.10F, 0.74F, 0.045F, 1.0F },
    glm::vec4 { 0.015F, 0.5F, 0.9F, 0.55F },
  };
  auto panel_geometry = BuildCubeGeometry(
    "SidednessPanel", { .rgba = { 0.20F, 0.22F, 0.25F, 1.0F } });
  const auto normal_map = validation_normal_map_ ? validation_normal_map_->Key()
                                                 : content::ResourceKey {};

  for (size_t row = 0; row < rows.size(); ++row) {
    const auto flags = row == 1 ? pak::render::kMaterialFlag_AlphaTest : 0U;
    const auto single_name = std::string(rows.at(row)) + "Single";
    const auto double_name = std::string(rows.at(row)) + "Double";
    auto material = MaterialSpec {
      .rgba = colors.at(row),
      .roughness = 0.75F,
      .domain = domains.at(row),
      .extra_flags = flags,
      .double_sided = false,
      .normal_map = normal_map,
    };
    auto single_geometry
      = BuildPrimitiveGeometry(single_name, vertices, { 0U, 1U, 2U }, material);
    material.double_sided = true;
    auto double_geometry
      = BuildPrimitiveGeometry(double_name, vertices, { 0U, 1U, 2U }, material);

    for (size_t column = 0; column < columns.size(); ++column) {
      const auto name = std::string(rows.at(row)) + "_" + columns.at(column);
      const auto position
        = glm::vec3 { (static_cast<float>(column) - 4.5F) * 2.3F, 0.0F,
            3.8F - (static_cast<float>(row) * 2.7F) };
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
  const auto controls = MaterialSpec {
    .name = "SidednessControls",
    .rgba = { 0.8F, 0.42F, 0.025F, 1.0F },
    .roughness = 0.55F,
    .double_sided = false,
  };
  auto cube = BuildPrimitiveGeometry("SidednessCube",
    std::move(cube_data->first), std::move(cube_data->second), controls);
  auto sphere = BuildPrimitiveGeometry("SidednessSphere",
    std::move(sphere_data->first), std::move(sphere_data->second), controls);
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
  light->Common().shadow.bias = kSunShadowBias;
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
  CreateScene("FixedExposureFixture", 8U);
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
      settings.compensation_curve
        = { { .metered_ev = 0.0F, .compensation_ev = -1.0e20F } };
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
    {
      .rgba = { 0.0F, 0.0F, 0.0F, 1.0F },
      .roughness = 1.0F,
      .emissive = glm::vec3 { input },
    }));
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
    light->Common().shadow.bias = kSunShadowBias;
    light->Common().color_rgb = { 1.0F, 0.97F, 0.92F };
    light->Common().shadow.contact_shadows
      = scene_options_.with_contact_shadows;
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
    light->Common().shadow.contact_shadows
      = scene_options_.with_contact_shadows;
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
    light->Common().shadow.contact_shadows
      = scene_options_.with_contact_shadows;
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
  // Game time: zero while the engine is paused, one fixed step per Step.
  const auto delta_seconds = context != nullptr
    ? std::chrono::duration<float>(context->GetGameDeltaTime().get()).count()
    : 0.0F;
  animation_time_seconds_ += delta_seconds;

  if (cube_node_.IsAlive()) {
    const float rotation_step_radians
      = kCubeRotationAnglePerSecond * delta_seconds;
    cube_rotation_ = glm::normalize(
      glm::angleAxis(rotation_step_radians, cube_rotation_axis_)
      * cube_rotation_);
    cube_node_.GetTransform().SetLocalPosition(kCubeCenter);
    cube_node_.GetTransform().SetLocalRotation(cube_rotation_);
  }
  if (occlusion_probe_node_.IsAlive()) {
    // Slides from behind the cube into view and back. Occlusion culling is
    // correct when the probe never pops while any part of it is visible.
    const float phase = (animation_time_seconds_ / kOcclusionProbePeriodSeconds)
      * glm::two_pi<float>();
    occlusion_probe_node_.GetTransform().SetLocalScale(kOcclusionProbeScale);
    occlusion_probe_node_.GetTransform().SetLocalPosition(kOcclusionProbeCenter
      + glm::vec3 { kOcclusionProbeSwing * std::sin(phase), 0.0F, 0.0F });
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
          -5.0F + (std::sin(spot_phase) * kSpotlightOscillationAmplitude),
          4.0F };
    spot_light_node_.GetTransform().SetLocalPosition(spot_position);
    spot_light_node_.GetTransform().SetLocalRotation(
      LookRotation(spot_position, kSceneFocusPoint));
  }
}

auto MainModule::ResolveViewExtent() const noexcept -> glm::uvec2
{
  if (const auto window = Window(); window && window->GetWindow()) {
    const auto extent = window->GetWindow()->Size();
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
    cam.SetFarPlane(chart_camera ? 100.0F : 2000.0F);
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

} // namespace oxygen::examples::vortex_basic
