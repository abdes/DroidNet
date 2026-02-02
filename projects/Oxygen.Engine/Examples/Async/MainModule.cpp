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
#include <ctime>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/ImGui/ImguiModule.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

#include "Async/AsyncDemoPanel.h"
#include "Async/AsyncDemoSettingsService.h"
#include "Async/AsyncDemoVm.h"
#include "Async/MainModule.h"
#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Runtime/SceneView.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/DroneCameraController.h"

using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::Scissors;
using oxygen::ViewPort;
using oxygen::data::Mesh;
using oxygen::data::Vertex;
using oxygen::engine::RenderItem;
using oxygen::graphics::Buffer;
using oxygen::graphics::Framebuffer;
using oxygen::scene::DistancePolicy;
using oxygen::scene::PerspectiveCamera;

namespace {
constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;

auto RotationFromForwardToDir(const glm::vec3& to_dir) -> glm::quat
{
  const glm::vec3 from_dir = oxygen::space::move::Forward;
  const glm::vec3 to = glm::normalize(to_dir);
  const float cos_theta = std::clamp(glm::dot(from_dir, to), -1.0F, 1.0F);

  if (cos_theta >= 0.9999F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  if (cos_theta <= -0.9999F) {
    // Opposite vectors: pick a stable orthogonal axis.
    const glm::vec3 axis = oxygen::space::move::Up;
    return glm::angleAxis(std::numbers::pi_v<float>, axis);
  }

  const glm::vec3 axis = glm::normalize(glm::cross(from_dir, to));
  const float angle = std::acos(cos_theta);
  return glm::angleAxis(angle, axis);
}

struct LocalTimeOfDay {
  int hour = 0;
  int minute = 0;
  int second = 0;
  double day_fraction = 0.0;
};

auto GetLocalTimeOfDayNow() -> LocalTimeOfDay
{
  const std::time_t t = std::time(nullptr);
  std::tm tm_local {};
  localtime_s(&tm_local, &t);

  LocalTimeOfDay tod;
  tod.hour = tm_local.tm_hour;
  tod.minute = tm_local.tm_min;
  tod.second = tm_local.tm_sec;
  const int seconds = (tod.hour * 3600) + (tod.minute * 60) + tod.second;
  tod.day_fraction = static_cast<double>(seconds) / 86400.0;
  return tod;
}

// Helper: make a solid-color material asset snapshot
auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque,
  bool double_sided = false)
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7; // MaterialAsset (for tooling/debug)
  // Safe copy name
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = double_sided ? pak::kMaterialFlag_DoubleSided : 0u;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = Unorm16 { 0.0F };
  desc.roughness = Unorm16 { 0.9F };
  desc.ambient_occlusion = Unorm16 { 1.0F };
  // Leave texture indices at default invalid (no textures)
  const AssetKey asset_key { .guid = GenerateAssetGuid() };
  return std::make_shared<const MaterialAsset>(
    asset_key, desc, std::vector<ShaderReference> {});
};

//! Build a 2-LOD sphere GeometryAsset (high and low tessellation).
auto BuildSphereLodAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  // Diagnostic toggle: force single-LOD spheres to rule out LOD switch pops
  // as a source of per-mesh stutter. Set to false to restore dual-LOD.
  constexpr bool kUseSingleLodForTest = true;

  // Semi-transparent material (transparent domain) with lower alpha to
  // accentuate blending against background.
  const auto glass = MakeSolidColorMaterial("Glass",
    { 0.2F, 0.6F, 0.9F, 0.35F }, oxygen::data::MaterialDomain::kAlphaBlended);

  // LOD 0: higher tessellation
  auto lod0_data = oxygen::data::MakeSphereMeshAsset(64, 64);
  CHECK_F(lod0_data.has_value());
  auto mesh0
    = MeshBuilder(0, "SphereLOD0")
        .WithVertices(lod0_data->first)
        .WithIndices(lod0_data->second)
        .BeginSubMesh("full", glass)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(lod0_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(lod0_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  // Optionally create LOD1
  std::shared_ptr<Mesh> mesh1;
  if (!kUseSingleLodForTest) {
    auto lod1_data = oxygen::data::MakeSphereMeshAsset(24, 24);
    CHECK_F(lod1_data.has_value());
    mesh1 = MeshBuilder(1, "SphereLOD1")
              .WithVertices(lod1_data->first)
              .WithIndices(lod1_data->second)
              .BeginSubMesh("full", glass)
              .WithMeshView(MeshViewDesc {
                .first_index = 0,
                .index_count = static_cast<uint32_t>(lod1_data->second.size()),
                .first_vertex = 0,
                .vertex_count = static_cast<uint32_t>(lod1_data->first.size()),
              })
              .EndSubMesh()
              .Build();
  }

  // Use LOD0 bounds for asset bounds
  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = kUseSingleLodForTest ? 1 : 2;
  const glm::vec3 bb_min = mesh0->BoundingBoxMin();
  const glm::vec3 bb_max = mesh0->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  if (kUseSingleLodForTest) {
    return std::make_shared<oxygen::data::GeometryAsset>(
      oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
      geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh0) });
  }

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc,
    std::vector<std::shared_ptr<Mesh>> { std::move(mesh0), std::move(mesh1) });
}

//! Build a 1-LOD mesh with two submeshes (two triangles of a quad).
auto BuildTwoSubmeshQuadAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  // Simple quad (XY plane), two triangles
  std::vector<Vertex> vertices;
  vertices.reserve(4);
  vertices.push_back(Vertex { .position = { -1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 } });
  vertices.push_back(Vertex { .position = { -1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 } });
  vertices.push_back(Vertex { .position = { 1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 } });
  vertices.push_back(Vertex { .position = { 1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 } });
  std::vector<uint32_t> indices { 0, 1, 2, 2, 1, 3 };

  // Create two distinct solid-color materials
  const auto red = MakeSolidColorMaterial("Red", { 1.0F, 0.1F, 0.1F, 1.0F },
    oxygen::data::MaterialDomain::kOpaque, true);
  const auto green = MakeSolidColorMaterial("Green", { 0.1F, 1.0F, 0.1F, 1.0F },
    oxygen::data::MaterialDomain::kOpaque, true);

  auto mesh = MeshBuilder(0, "Quad2SM")
                .WithVertices(vertices)
                .WithIndices(indices)
                // Submesh 0: first triangle (opaque red)
                .BeginSubMesh("tri0", red)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                // Submesh 1: second triangle (opaque green restored)
                .BeginSubMesh("tri1", green)
                .WithMeshView(MeshViewDesc {
                  .first_index = 3,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  // Geometry asset with 1 LOD
  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh) });
}

// ----------------- Camera spline helpers (closed Catmull-Rom)
// -----------------

// Evaluate closed Catmull-Rom spline at parameter u in [0,1). Control points
// must have size >= 4
auto EvalClosedCatmullRom(const std::vector<glm::vec3>& pts, double u)
  -> glm::vec3
{
  const size_t n = pts.size();
  if (n == 0) {
    return glm::vec3(0.0F);
  }
  if (n < 4) {
    return pts[0];
  }
  // Map u to segment index
  const double total = u * static_cast<double>(n);
  int i0 = static_cast<int>(std::floor(total));
  double local_t = total - static_cast<double>(i0);
  i0 = i0 % static_cast<int>(n);
  if (i0 < 0) {
    i0 += static_cast<int>(n);
  }

  const int i1 = (i0 + 1) % static_cast<int>(n);
  const int i_1 = (i0 - 1 + static_cast<int>(n)) % static_cast<int>(n);
  const int i2 = (i0 + 2) % static_cast<int>(n);

  const auto P0 = glm::dvec3(pts[i_1]);
  const auto P1 = glm::dvec3(pts[i0]);
  const auto P2 = glm::dvec3(pts[i1]);
  const auto P3 = glm::dvec3(pts[i2]);

  const double t = local_t;
  const double t2 = t * t;
  const double t3 = t2 * t;

  // Catmull-Rom basis
  const glm::dvec3 res = 0.5
    * ((2.0 * P1) + (-P0 + P2) * t + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * t2
      + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * t3);
  return glm::vec3(static_cast<float>(res.x), static_cast<float>(res.y),
    static_cast<float>(res.z));
}

// Build an arc-length lookup table for a closed Catmull-Rom spline.
// Returns cumulative lengths (s) and corresponding parameters (u).
auto BuildArcLengthLut(const std::vector<glm::vec3>& pts, int samples,
  std::vector<double>& out_u, std::vector<double>& out_s) -> void
{
  out_u.clear();
  out_s.clear();
  if (pts.size() < 4 || samples < 2) {
    return;
  }

  out_u.reserve(static_cast<size_t>(samples) + 1);
  out_s.reserve(static_cast<size_t>(samples) + 1);

  double s = 0.0;
  glm::vec3 prev = EvalClosedCatmullRom(pts, 0.0);
  out_u.push_back(0.0);
  out_s.push_back(0.0);
  for (int i = 1; i <= samples; ++i) {
    const double u = static_cast<double>(i) / static_cast<double>(samples);
    const glm::vec3 p = EvalClosedCatmullRom(pts, u);
    s += glm::length(p - prev);
    out_u.push_back(u);
    out_s.push_back(s);
    prev = p;
  }
}

// Given an arc-length s in [0, total_len), find u in [0,1) using the LUT.
auto ArcLengthToParamU(double s, const std::vector<double>& u_samples,
  const std::vector<double>& s_samples) -> double
{
  if (u_samples.empty() || s_samples.empty()) {
    return 0.0;
  }
  const double total = s_samples.back();
  if (total <= 0.0) {
    return 0.0;
  }
  // Wrap s into [0,total)
  s = std::fmod(s, total);
  if (s < 0.0) {
    s += total;
  }

  // Binary search for segment
  auto it = std::lower_bound(s_samples.begin(), s_samples.end(), s);
  size_t idx = static_cast<size_t>(std::distance(s_samples.begin(), it));
  if (idx == 0) {
    return u_samples.front();
  }
  if (idx >= s_samples.size()) {
    return u_samples.back();
  }

  const double s0 = s_samples[idx - 1];
  const double s1 = s_samples[idx];
  const double u0 = u_samples[idx - 1];
  const double u1 = u_samples[idx];
  const double t = (s1 > s0) ? ((s - s0) / (s1 - s0)) : 0.0;
  return u0 + t * (u1 - u0);
}

// Approximate path length by sampling
auto ApproximatePathLength(const std::vector<glm::vec3>& pts, int samples = 256)
  -> double
{
  if (pts.empty()) {
    return 0.0;
  }
  double len = 0.0;
  glm::vec3 prev = EvalClosedCatmullRom(pts, 0.0);
  for (int i = 1; i <= samples; ++i) {
    double u = static_cast<double>(i) / static_cast<double>(samples);
    glm::vec3 p = EvalClosedCatmullRom(pts, u);
    len += glm::length(p - prev);
    prev = p;
  }
  return len;
}

//! Update camera position along a smooth orbit.
// Fixed camera: positioned on a circle at 45deg pitch looking at origin.
auto SetupFixedCamera(oxygen::scene::SceneNode& camera_node) -> void
{
  constexpr float radius = 15.0F;
  constexpr float pitch_deg = 10.0F;
  constexpr float pitch = glm::radians(pitch_deg);
  // Place camera on negative Z so quad (facing +Z) is front-facing.
  const glm::vec3 position(
    radius * 0.0F, radius * std::sin(pitch), -radius * std::cos(pitch));
  auto transform = camera_node.GetTransform();
  transform.SetLocalPosition(position);
  constexpr glm::vec3 target(0.0F);
  constexpr glm::vec3 up(0.0F, 1.0F, 0.0F);
  const glm::vec3 dir = glm::normalize(target - position);
  transform.SetLocalRotation(glm::quatLookAtRH(dir, up));
}

// Convert hue [0,1] to an RGB color (simple H->RGB approx)
auto ColorFromHue(double h) -> glm::vec3
{
  // h in [0,1)
  const double hh = std::fmod(h, 1.0);
  const double r = std::abs(hh * 6.0 - 3.0) - 1.0;
  const double g = 2.0 - std::abs(hh * 6.0 - 2.0);
  const double b = 2.0 - std::abs(hh * 6.0 - 4.0);
  return glm::vec3(static_cast<float>(std::clamp(r, 0.0, 1.0)),
    static_cast<float>(std::clamp(g, 0.0, 1.0)),
    static_cast<float>(std::clamp(b, 0.0, 1.0)));
}

// Orbit sphere around origin on XZ plane with custom radius.
auto AnimateSphereOrbit(oxygen::scene::SceneNode& sphere_node, double angle,
  double radius, double inclination, double spin_angle) -> void
{
  // Position in XY plane first (XZ orbit, y=0)
  const double x = radius * std::cos(angle);
  const double z = radius * std::sin(angle);
  // Tilt the orbital plane by applying a rotation around the X axis
  const glm::dvec3 pos_local(x, 0.0, z);
  const double ci = std::cos(inclination);
  const double si = std::sin(inclination);
  // Rotation matrix for tilt around X: [1 0 0; 0 ci -si; 0 si ci]
  const glm::dvec3 pos_tilted(pos_local.x, pos_local.y * ci - pos_local.z * si,
    pos_local.y * si + pos_local.z * ci);
  const glm::vec3 pos(static_cast<float>(pos_tilted.x),
    static_cast<float>(pos_tilted.y), static_cast<float>(pos_tilted.z));

  if (!sphere_node.IsAlive()) {
    return;
  }

  // Set translation
  sphere_node.GetTransform().SetLocalPosition(pos);

  // Apply self-rotation (spin) around local Y axis
  const glm::quat spin_quat = glm::angleAxis(
    static_cast<float>(spin_angle), glm::vec3(0.0F, 1.0F, 0.0F));
  sphere_node.GetTransform().SetLocalRotation(spin_quat);
}

} // namespace

namespace oxygen::examples::async {

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
  , app_(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());

  // Record start time for animations (use time_point for robust delta)
  start_time_ = std::chrono::steady_clock::now();
}

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  DCHECK_NOTNULL_F(engine);

  if (!Base::OnAttached(engine)) {
    return false;
  }

  // Create Pipeline
  pipeline_ = std::make_unique<oxygen::examples::ForwardPipeline>(
    observer_ptr { app_.engine.get() });

  shell_ = std::make_unique<DemoShell>();

  settings_service_ = std::make_shared<AsyncDemoSettingsService>();
  vm_ = std::make_shared<AsyncDemoVm>(observer_ptr { settings_service_.get() },
    observer_ptr { &camera_spot_light_ }, &current_frame_tracker_, &spheres_);
  vm_->SetEnsureSpotlightCallback([this]() { EnsureCameraSpotLight(); });

  async_panel_ = std::make_shared<AsyncDemoPanel>(observer_ptr { vm_.get() });

  DemoShellConfig shell_config;
  shell_config.engine = observer_ptr { app_.engine.get() };
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = true,
    .environment = true,
    .lighting = true,
    .rendering = true,
  };
  shell_config.enable_camera_rig = true;
  shell_config.get_active_pipeline
    = [this]() -> observer_ptr<oxygen::examples::RenderingPipeline> {
    return observer_ptr { pipeline_.get() };
  };

  if (!shell_->Initialize(shell_config)) {
    LOG_F(WARNING, "Async: DemoShell initialization failed");
    return false;
  }

  if (!shell_->RegisterPanel(async_panel_)) {
    LOG_F(WARNING, "Async: failed to register Async panel");
  }

  EnsureExampleScene();

  // Create Main View (using placeholder, updated in OnSceneMutation)
  auto view = std::make_unique<SceneView>(scene::SceneNode {});
  main_view_ = observer_ptr(static_cast<SceneView*>(AddView(std::move(view))));

  // --- ImGuiPass configuration ---
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (app_window_) {
      imgui_module.SetWindowId(app_window_->GetWindowId());
    }
  }

  // ConfigureDrone removed from here, moved to HandleOnFrameStart

  initialized_ = true;
  return true;
}

MainModule::~MainModule() { active_scene_ = {}; }

auto MainModule::GetSupportedPhases() const noexcept -> engine::ModulePhaseMask
{
  using namespace core;
  return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
    PhaseId::kTransformPropagation, PhaseId::kGuiUpdate, PhaseId::kPreRender,
    PhaseId::kRender, PhaseId::kCompositing, PhaseId::kFrameEnd>();
}

auto MainModule::BuildDefaultWindowProperties() const
  -> oxygen::platform::window::Properties
{
  oxygen::platform::window::Properties props(
    "Oxygen Graphics Demo - AsyncEngine");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  return props;
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnExampleFrameStart");

  // Register scene with frame context (required for rendering)

  // Register scene with frame context (required for rendering)
  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  if (scene_ptr) {
    context.SetScene(oxygen::observer_ptr { scene_ptr.get() });
  }

  // Ensure drone is configured once the rig is available
  const auto rig = shell_ ? shell_->GetCameraRig()
                          : observer_ptr<ui::CameraRigController> { nullptr };
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
    drone_configured_ = false;
  }
  if (!drone_configured_ && rig) {
    ConfigureDrone();
    drone_configured_ = true;
  }
}

void MainModule::OnShutdown() noexcept
{
  if (shell_) {
    shell_->SetScene(std::unique_ptr<scene::Scene> {});
  }
  active_scene_ = {};

  async_panel_.reset();
  vm_.reset();
  settings_service_.reset();
  shell_.reset();
  shell_.reset();
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  StartFrameTracking();
  TrackFrameAction("Frame started");

  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    TrackFrameAction("GUI update skipped - app window not available");
    // continue to Base to ensure cleanup
  }

  LOG_SCOPE_F(3, "MainModule::OnFrameStart");

  // Call base to handle window lifecycle and surface setup
  Base::OnFrameStart(context);
}

// Initialize a default looping flight path over the scene (few control points)

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_F(active_scene_.IsValid());
  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  DCHECK_NOTNULL_F(scene_ptr);
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    // Window invalid, skip update
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    TrackFrameAction("Scene mutation skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnSceneMutation");
  TrackPhaseStart("Scene Mutation");
  current_frame_tracker_.scene_mutation_occurred = true;
  TrackFrameAction("Scene mutation phase started");

  if (app_window_ && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    EnsureMainCamera(width, height);
    EnsureCameraSpotLight();
    if (shell_) {
      auto& camera_lifecycle = shell_->GetCameraLifecycle();
      auto& active_camera = camera_lifecycle.GetActiveCamera();
      if (main_camera_.IsAlive()) {
        const bool needs_activation = !active_camera.IsAlive()
          || active_camera.GetHandle() != main_camera_.GetHandle();
        if (needs_activation) {
          shell_->SetActiveCamera(main_camera_);
        }
      }
    }
    EnsureViewCameraRegistered();
  }

  // Update view camera from shell's active camera
  if (main_view_ && shell_) {
    auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
    if (active_camera.IsAlive()) {
      main_view_->SetCamera(active_camera);
    }
  }

  // Handle scene mutations (material overrides, visibility changes)
  // Use the engine-provided frame start time so all modules use a
  // consistent timestamp for this frame.
  const auto now = context.GetFrameStartTime();
  const float delta_time
    = std::chrono::duration<float>(now - start_time_).count();
  UpdateSceneMutations(delta_time);

  if (shell_) {
    // Use engine-provided game delta time for physics/input
    shell_->Update(context.GetGameDeltaTime());
  }

  TrackFrameAction("Scene mutations updated");

  // Delegate to base class to register views with the pipeline and renderer
  co_await Base::OnSceneMutation(context);

  TrackPhaseEnd();
  co_return;
}

auto MainModule::EnsureCameraSpotLight() -> void
{
  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  auto* scene = scene_ptr.get();
  if (!scene || !main_camera_.IsAlive()) {
    return;
  }

  if (!camera_spot_light_.IsAlive()) {
    auto child_opt = scene->CreateChildNode(main_camera_, "CameraSpotLight");
    if (!child_opt.has_value()) {
      return;
    }
    camera_spot_light_ = std::move(child_opt.value());
    camera_spot_light_.GetTransform().SetLocalPosition(glm::vec3(0.0F));
    // Camera rotation is produced by glm::quatLookAtRH(), which treats local
    // forward as -Z. Lights use engine forward = -Y.
    // Apply a constant correction so the spotlight points exactly where the
    // camera looks.
    const glm::quat spot_correction
      = glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0F, 0.0F, 0.0F));
    camera_spot_light_.GetTransform().SetLocalRotation(spot_correction);
  }

  if (camera_spot_light_.IsAlive() && !camera_spot_light_.HasLight()) {
    auto light = std::make_unique<scene::SpotLight>();
    const float intensity
      = settings_service_ ? settings_service_->GetSpotlightIntensity() : 300.0F;
    const float range
      = settings_service_ ? settings_service_->GetSpotlightRange() : 35.0F;
    const glm::vec3 color = settings_service_
      ? settings_service_->GetSpotlightColor()
      : glm::vec3(1.0F, 1.0F, 1.0F);
    const float inner_cone = settings_service_
      ? settings_service_->GetSpotlightInnerCone()
      : glm::radians(12.0F);
    const float outer_cone = settings_service_
      ? settings_service_->GetSpotlightOuterCone()
      : glm::radians(26.0F);
    const bool enabled
      = settings_service_ ? settings_service_->GetSpotlightEnabled() : true;
    const bool casts_shadows = settings_service_
      ? settings_service_->GetSpotlightCastsShadows()
      : false;

    light->Common().affects_world = enabled;
    light->Common().color_rgb = color;
    light->Common().intensity = intensity;
    light->Common().mobility = scene::LightMobility::kRealtime;
    light->Common().casts_shadows = casts_shadows;
    light->SetRange(range);
    light->SetAttenuationModel(scene::AttenuationModel::kInverseSquare);
    const float clamped_inner = std::min(inner_cone, outer_cone);
    const float clamped_outer = std::max(inner_cone, outer_cone);
    light->SetConeAnglesRadians(clamped_inner, clamped_outer);
    light->SetSourceRadius(0.0F);

    const bool attached = camera_spot_light_.ReplaceLight(std::move(light));
    CHECK_F(attached, "Failed to attach SpotLight to CameraSpotLight");
  }
}

auto MainModule::OnTransformPropagation(engine::FrameContext& context)
  -> co::Co<>
{
  TrackPhaseStart("Transform Propagation");

  // Ensure framebuffers are created after a resize
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    TrackFrameAction("GUI update skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnTransformPropagation");
  current_frame_tracker_.transform_propagation_occurred = true;
  TrackFrameAction("Transform propagation phase started");

  // Update animations and transforms (no scene mutations)
  // Compute per-frame delta from engine frame timestamp. Clamp delta to a
  // reasonable maximum to avoid large jumps when the app was paused or a
  // long hiccup occurred.
  const auto now = context.GetFrameStartTime();
  double delta_seconds = 0.0;
  if (last_frame_time_.time_since_epoch().count() == 0) {
    // First frame observed by module: initialize last_frame_time_
    last_frame_time_ = now;
    delta_seconds = 0.0;
  } else {
    delta_seconds
      = std::chrono::duration<double>(now - last_frame_time_).count();
  }
  // Cap delta to, e.g., 50ms to avoid teleporting when resuming from pause.
  constexpr double kMaxDelta = 0.05;
  if (delta_seconds > kMaxDelta) {
    delta_seconds = kMaxDelta;
  }

  // Compute per-frame delta_time and forward to UpdateAnimations (double)
  const double delta_time = delta_seconds;
  UpdateAnimations(delta_time);

  // Store last frame timestamp for next update
  last_frame_time_ = now;

  current_frame_tracker_.spheres_updated
    = static_cast<std::uint32_t>(spheres_.size());
  TrackFrameAction("Animations and transforms updated");
  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  TrackPhaseStart("PreRender");

  // Ensure framebuffers are created after a resize
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    TrackFrameAction("GUI update skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnPreRender");

  // Set ImGui context before making ImGui calls
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  current_frame_tracker_.frame_graph_setup = true;
  TrackFrameAction("Pre-render setup started");

  if (!app_window_ || !app_window_->GetWindow()) {
    DLOG_F(1, "OnPreRender: no valid window - skipping");
    TrackPhaseEnd();
    co_return;
  }

  // Configure pass-specific settings (clear color, debug names, etc.)
  if (pipeline_) {
    oxygen::engine::ShaderPassConfig config;
    config.clear_color = oxygen::graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
    config.debug_name = "ShaderPass";
    pipeline_->UpdateShaderPassConfig(config);
  }

  TrackFrameAction("Frame graph and render passes configured");

  // Delegate to base class to execute pipeline OnPreRender (which configures
  // passes)
  co_await Base::OnPreRender(context);

  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  TrackPhaseStart("Compositing");

  // Ensure framebuffers are created after a resize
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnCompositing: no valid window - skipping");
    TrackFrameAction("Compositing skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  co_await Base::OnCompositing(context);
  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  TrackPhaseStart("GUI Update");

  // Window must be available to render GUI
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow() || app_window_->IsShuttingDown()) {
    TrackFrameAction("GUI update skipped - app window not available/closing");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnGuiUpdate");

  if (shell_) {
    shell_->Draw(context);
  }

  TrackFrameAction("GUI overlay built");
  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnRender(engine::FrameContext& context) -> co::Co<>
{
  TrackPhaseStart("Render");

  // Ensure framebuffers are created after a resize
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    TrackFrameAction("GUI update skipped - app window not available");
    TrackPhaseEnd();
    co_return;
  }

  LOG_SCOPE_F(3, "MainModule::OnRender");
  current_frame_tracker_.command_recording = true;
  TrackFrameAction("Render started");

  // Renderer now handles all command recording automatically via registered
  // render graph factories. App module just tracks that rendering happened.

  TrackFrameAction("Render delegated to Renderer module");
  TrackPhaseEnd();
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnFrameEnd");

  TrackFrameAction("Frame ended");
  EndFrameTracking();
}

auto MainModule::EnsureExampleScene() -> void
{
  if (active_scene_.IsValid()) {
    return;
  }

  using scene::Scene;

  auto scene = std::make_unique<Scene>("ExampleScene");

  if (shell_) {
    active_scene_ = shell_->SetScene(std::move(scene));
  }

  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  auto* scene_raw = scene_ptr.get();
  CHECK_NOTNULL_F(scene_raw, "Async: active scene not available");

  // Create a LOD sphere and a multi-submesh quad
  auto sphere_geo = BuildSphereLodAsset();
  auto quad2sm_geo = BuildTwoSubmeshQuadAsset();

  // Create multiple spheres; initial positions will be set by orbit.
  // Diagnostic toggles:
  constexpr bool kDisableSphereLodPolicy = true; // avoid LOD switch hitches
  constexpr bool kForceOpaqueSpheres = false; // set true to avoid sorting
  // Use a small number for performance while still demonstrating behavior.
  constexpr std::size_t kNumSpheres = 16;
  spheres_.reserve(kNumSpheres);
  // Seeded RNG for reproducible variation across runs
  std::mt19937 rng(123456789);
  std::uniform_real_distribution<double> speed_dist(0.2, 1.2);
  std::uniform_real_distribution<double> radius_dist(2.0, 8.0);
  std::uniform_real_distribution<double> phase_jitter(-0.25, 0.25);
  std::uniform_real_distribution<double> hue_dist(0.0, 1.0);
  std::uniform_real_distribution<double> incl_dist(-0.9, 0.9); // ~-51..51 deg
  std::uniform_real_distribution<double> spin_dist(-2.0, 2.0); // rad/s
  std::uniform_real_distribution<double> transp_dist(0.0, 1.0);

  for (std::size_t i = 0; i < kNumSpheres; ++i) {
    const std::string name = std::string("Sphere_") + std::to_string(i);
    auto node = scene_raw->CreateNode(name.c_str());
    node.GetRenderable().SetGeometry(sphere_geo);

    // Enlarge sphere to better showcase transparency layering against
    // background
    if (node.IsAlive()) {
      node.GetTransform().SetLocalScale(glm::vec3(3.0F));
    }

    // Configure LOD policy per-sphere (disabled during diagnostics)
    if (!kDisableSphereLodPolicy) {
      auto r = node.GetRenderable();
      DistancePolicy pol;
      pol.thresholds = { 6.2F }; // switch LOD0->1 around ~6.2
      pol.hysteresis_ratio = 0.08F; // modest hysteresis to avoid flicker
      r.SetLodPolicy(std::move(pol));
    }

    // Randomized parameters: seed ensures reproducible runs
    constexpr double two_pi = glm::two_pi<float>();
    const double base_phase
      = (two_pi * static_cast<double>(i)) / static_cast<double>(kNumSpheres);
    const double jitter = phase_jitter(rng);
    const double init_angle = base_phase + jitter;
    const double speed = speed_dist(rng);
    const double radius = radius_dist(rng);
    const double hue = hue_dist(rng);

    // Apply per-sphere material override (transparent glass-like)
    auto r = node.GetRenderable();
    const std::string mat_name = std::string("SphereMat_") + std::to_string(i);
    const auto rgb = ColorFromHue(hue);
    const bool is_transparent
      = kForceOpaqueSpheres ? false : (transp_dist(rng) < 0.5);
    const float alpha = is_transparent ? 0.35F : 1.0F;
    const auto domain = is_transparent ? data::MaterialDomain::kAlphaBlended
                                       : data::MaterialDomain::kOpaque;
    const glm::vec4 color(rgb.x, rgb.y, rgb.z, alpha);
    const auto mat = MakeSolidColorMaterial(mat_name.c_str(), color, domain);
    // Apply override for submesh index 0 across all LODs so switching LOD
    // retains the material override. Use EffectiveLodCount() to iterate.
    const auto lod_count = r.EffectiveLodCount();
    for (std::size_t lod = 0; lod < lod_count; ++lod) {
      r.SetMaterialOverride(lod, 0, mat);
    }

    SphereState s;
    s.node = node;
    s.base_angle = init_angle;
    s.speed = speed;
    s.radius = radius;
    s.inclination = incl_dist(rng);
    s.spin_speed = spin_dist(rng);
    s.base_spin_angle = 0.0;
    spheres_.push_back(std::move(s));
  }

  // Multi-submesh quad centered at origin facing +Z (already in XY plane)
  multisubmesh_ = scene_raw->CreateNode("MultiSubmesh");
  multisubmesh_.GetRenderable().SetGeometry(quad2sm_geo);
  multisubmesh_.GetTransform().SetLocalPosition(glm::vec3(0.0F));
  multisubmesh_.GetTransform().SetLocalRotation(glm::quat(1, 0, 0, 0));

  // Set up a default flight path for the camera drone

  LOG_F(
    INFO, "Scene created: SphereDistance (LOD) and MultiSubmesh (per-submesh)");
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;

  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  auto* scene = scene_ptr.get();
  if (!scene) {
    return;
  }

  if (!main_camera_.IsAlive()) {
    main_camera_ = scene->CreateNode("MainCamera");
  }

  if (!main_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = main_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  // Configure camera params
  const auto cam_ref = main_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(45.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(600.0F);
    cam.SetViewport(ViewPort { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F });
  }
}

auto MainModule::ConfigureDrone() -> void
{
  if (!shell_ || !shell_->GetCameraRig()) {
    return;
  }

  auto drone_controller = shell_->GetCameraRig()->GetDroneController();
  if (!drone_controller) {
    return;
  }

  // Set path generator
  drone_controller->SetPathGenerator([]() -> std::vector<glm::vec3> {
    constexpr int points = 96;
    constexpr float a = 36.0F;
    constexpr float altitude = 14.0F;
    std::vector<glm::vec3> path;
    path.reserve(points);
    for (int i = 0; i < points; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(points);
      const float ang = t * glm::two_pi<float>();
      const float x = a * std::cos(ang);
      const float z = a * std::sin(ang) * std::cos(ang);
      path.push_back(glm::vec3(x, altitude, z));
    }
    return path;
  });

  // Settings matching previous defaults
  drone_controller->SetSpeed(6.0);
  drone_controller->SetDamping(8.0);
  drone_controller->SetRampTime(2.0);
  drone_controller->SetBobAmplitude(0.06);
  drone_controller->SetBobFrequency(1.6);
  drone_controller->SetNoiseAmplitude(0.03);
  drone_controller->SetBankFactor(0.045);
  drone_controller->SetMaxBank(0.45);
  drone_controller->SetFocusHeight(0.8F);

  // Switch to drone mode
  shell_->GetCameraRig()->SetMode(
    oxygen::examples::ui::CameraControlMode::kDrone);
  drone_controller->Start();
}

auto MainModule::EnsureViewCameraRegistered() -> void
{
  if (!shell_)
    return;

  // Use TryGetScene to avoid creating one if missing
  const auto scene = shell_->TryGetScene();
  // We need a valid scene for the camera to be meaningful in many cases,
  // but DemoShell manages the camera availability independent of scene mostly.

  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive())
    return;

  if (main_view_) {
    bool needs_refresh = true;
    if (const auto current = main_view_->GetCamera()) {
      if (current->IsAlive()) {
        needs_refresh = current->GetHandle() != active_camera.GetHandle();
      }
    }
    if (needs_refresh) {
      main_view_->SetCamera(active_camera);
      // Force update to ensure pipeline picks it up
      main_view_->SetRendererRegistered(false);
    }
  }
}

auto MainModule::UpdateAnimations(double delta_time) -> void
{
  // delta_time is the elapsed time since last frame in seconds (double).
  // Clamp large deltas to avoid jumps after pause/hitch (50 ms max)
  constexpr double kMaxDelta = 0.05;
  const double effective_dt = (delta_time > kMaxDelta) ? kMaxDelta : delta_time;

  constexpr double two_pi = glm::two_pi<float>();

  // Absolute-time sampling for deterministic, jitter-free animation
  anim_time_ += effective_dt;
  for (auto& s : spheres_) {
    const double angle = std::fmod(s.base_angle + s.speed * anim_time_, two_pi);
    const double spin
      = std::fmod(s.base_spin_angle + s.spin_speed * anim_time_, two_pi);
    AnimateSphereOrbit(s.node, angle, s.radius, s.inclination, spin);
  }

  if (multisubmesh_.IsAlive()) {
    constexpr double kQuadSpinSpeed = 0.6; // radians/sec
    const double quad_angle = std::fmod(anim_time_ * kQuadSpinSpeed, two_pi);
    const glm::quat quad_rot = glm::angleAxis(
      static_cast<float>(quad_angle), glm::vec3(0.0F, 1.0F, 0.0F));
    multisubmesh_.GetTransform().SetLocalRotation(quad_rot);
  }

  // Periodic lightweight logging
  static int dbg_counter = 0;
  ++dbg_counter;
  if ((dbg_counter % 120) == 0) {
    LOG_F(INFO, "[Anim] delta_time={}ms spheres={}", delta_time * 1000.0,
      static_cast<int>(spheres_.size()));
  }
}

auto MainModule::UpdateSceneMutations(const float delta_time) -> void
{
  // Toggle per-submesh visibility and material override over time
  if (multisubmesh_.IsAlive()) {
    auto r = multisubmesh_.GetRenderable();
    constexpr std::size_t lod = 0;

    // Every 2 seconds, toggle submesh 0 visibility
    int vis_phase = static_cast<int>(delta_time) / 2;
    if (vis_phase != last_vis_toggle_) {
      last_vis_toggle_ = vis_phase;
      const bool visible = (vis_phase % 2) == 0;
      r.SetSubmeshVisible(lod, 0, visible);
      LOG_F(INFO, "[MultiSubmesh] Submesh 0 visibility -> {}", visible);
    }

    // Every second, toggle an override on submesh 1 (use blue instead of
    // green)
    int ovr_phase = static_cast<int>(delta_time);
    if (ovr_phase != last_ovr_toggle_) {
      last_ovr_toggle_ = ovr_phase;
      const bool apply_override = (ovr_phase % 2) == 1;
      if (apply_override) {
        data::pak::MaterialAssetDesc desc {};
        desc.header.asset_type = 7;
        auto name = "BlueOverride";
        constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
        const std::size_t n = (std::min)(maxn, std::strlen(name));
        std::memcpy(desc.header.name, name, n);
        desc.header.name[n] = '\0';
        desc.material_domain
          = static_cast<uint8_t>(data::MaterialDomain::kOpaque);
        desc.base_color[0] = 0.2F;
        desc.base_color[1] = 0.3F;
        desc.base_color[2] = 1.0F;
        desc.base_color[3] = 1.0F;
        const data::AssetKey asset_key { .guid = data::GenerateAssetGuid() };
        auto blue = std::make_shared<const data::MaterialAsset>(
          asset_key, desc, std::vector<data::ShaderReference> {});
        r.SetMaterialOverride(lod, 1, blue);
      } else {
        r.ClearMaterialOverride(lod, 1);
      }
      LOG_F(INFO, "[MultiSubmesh] Submesh 1 override -> {}",
        apply_override ? "blue" : "clear");
    }
  }
}

// DrawSpotLightPanel removed

auto MainModule::TrackPhaseStart(const std::string& phase_name) -> void
{
  phase_start_time_ = std::chrono::steady_clock::now();
  current_phase_name_ = phase_name;
}

auto MainModule::TrackPhaseEnd() -> void
{
  if (phase_start_time_ != std::chrono::steady_clock::time_point {}) {
    const auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - phase_start_time_);

    // Add timing to current frame tracker
    current_frame_tracker_.phase_timings.emplace_back(
      current_phase_name_, duration);

    // Reset for next phase
    phase_start_time_ = std::chrono::steady_clock::time_point {};
    current_phase_name_.clear();
  }
}

auto MainModule::TrackFrameAction(const std::string& action) -> void
{
  current_frame_tracker_.frame_actions.push_back(action);
}

auto MainModule::StartFrameTracking() -> void
{
  current_frame_tracker_ = FrameActionTracker {};
  current_frame_tracker_.frame_start_time = std::chrono::steady_clock::now();
}

auto MainModule::EndFrameTracking() -> void
{
  current_frame_tracker_.frame_end_time = std::chrono::steady_clock::now();

  // Calculate total frame time if we don't have phase timings
  if (current_frame_tracker_.phase_timings.empty()) {
    const auto total_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        current_frame_tracker_.frame_end_time
        - current_frame_tracker_.frame_start_time);
    current_frame_tracker_.phase_timings.emplace_back(
      "Total Frame", total_duration);
  }

  // Add to history and maintain size limit
  frame_history_.push_back(current_frame_tracker_);
  if (frame_history_.size() > kMaxFrameHistory) {
    frame_history_.erase(frame_history_.begin());
  }
}

} // namespace oxygen::examples::async
