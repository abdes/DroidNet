//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Passes/TransparentPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

using oxygen::examples::async::MainModule;
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

namespace {
constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;

// Helper: make a solid-color material asset snapshot
auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
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
  desc.flags = 0;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0f;
  desc.metalness = 0.0f;
  desc.roughness = 0.9f;
  desc.ambient_occlusion = 1.0f;
  // Leave texture indices at default invalid (no textures)
  return std::make_shared<const MaterialAsset>(
    desc, std::vector<ShaderReference> {});
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
    { 0.2f, 0.6f, 0.9f, 0.35f }, oxygen::data::MaterialDomain::kAlphaBlended);

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
      geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh0) });
  }

  return std::make_shared<oxygen::data::GeometryAsset>(geo_desc,
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
  const auto red = MakeSolidColorMaterial("Red", { 1.0f, 0.1f, 0.1f, 1.0f });
  const auto green
    = MakeSolidColorMaterial("Green", { 0.1f, 1.0f, 0.1f, 1.0f });

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
    geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh) });
}

// ----------------- Camera spline helpers (closed Catmull-Rom)
// -----------------

// Evaluate closed Catmull-Rom spline at parameter u in [0,1). Control points
// must have size >= 4
static glm::vec3 EvalClosedCatmullRom(
  const std::vector<glm::vec3>& pts, double u)
{
  const size_t n = pts.size();
  if (n == 0)
    return glm::vec3(0.0f);
  if (n < 4)
    return pts[0];
  // Map u to segment index
  const double total = u * static_cast<double>(n);
  int i0 = static_cast<int>(std::floor(total));
  double local_t = total - static_cast<double>(i0);
  i0 = i0 % static_cast<int>(n);
  if (i0 < 0)
    i0 += static_cast<int>(n);

  const int i1 = (i0 + 1) % static_cast<int>(n);
  const int i_1 = (i0 - 1 + static_cast<int>(n)) % static_cast<int>(n);
  const int i2 = (i0 + 2) % static_cast<int>(n);

  const glm::dvec3 P0 = glm::dvec3(pts[i_1]);
  const glm::dvec3 P1 = glm::dvec3(pts[i0]);
  const glm::dvec3 P2 = glm::dvec3(pts[i1]);
  const glm::dvec3 P3 = glm::dvec3(pts[i2]);

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
static void BuildArcLengthLut(const std::vector<glm::vec3>& pts, int samples,
  std::vector<double>& out_u, std::vector<double>& out_s)
{
  out_u.clear();
  out_s.clear();
  if (pts.size() < 4 || samples < 2)
    return;

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
static double ArcLengthToParamU(double s, const std::vector<double>& u_samples,
  const std::vector<double>& s_samples)
{
  if (u_samples.empty() || s_samples.empty())
    return 0.0;
  const double total = s_samples.back();
  if (total <= 0.0)
    return 0.0;
  // Wrap s into [0,total)
  s = std::fmod(s, total);
  if (s < 0.0)
    s += total;

  // Binary search for segment
  auto it = std::lower_bound(s_samples.begin(), s_samples.end(), s);
  size_t idx = static_cast<size_t>(std::distance(s_samples.begin(), it));
  if (idx == 0)
    return u_samples.front();
  if (idx >= s_samples.size())
    return u_samples.back();

  const double s0 = s_samples[idx - 1];
  const double s1 = s_samples[idx];
  const double u0 = u_samples[idx - 1];
  const double u1 = u_samples[idx];
  const double t = (s1 > s0) ? ((s - s0) / (s1 - s0)) : 0.0;
  return u0 + t * (u1 - u0);
}

// Approximate path length by sampling
static double ApproximatePathLength(
  const std::vector<glm::vec3>& pts, int samples = 256)
{
  if (pts.empty())
    return 0.0;
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
  const float pitch = glm::radians(pitch_deg);
  // Place camera on negative Z so quad (facing +Z) is front-facing.
  const glm::vec3 position(
    radius * 0.0F, radius * std::sin(pitch), -radius * std::cos(pitch));
  auto transform = camera_node.GetTransform();
  transform.SetLocalPosition(position);
  const glm::vec3 target(0.0F);
  const glm::vec3 up(0.0F, 1.0F, 0.0F);
  const glm::vec3 dir = glm::normalize(target - position);
  transform.SetLocalRotation(glm::quatLookAtRH(dir, up));
}

// Convert hue [0,1] to an RGB color (simple H->RGB approx)
static glm::vec3 ColorFromHue(double h)
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
    static_cast<float>(spin_angle), glm::vec3(0.0f, 1.0f, 0.0f));
  sphere_node.GetTransform().SetLocalRotation(spin_quat);
}

} // namespace

MainModule::MainModule(std::shared_ptr<Platform> platform,
  std::weak_ptr<Graphics> gfx_weak, bool fullscreen,
  observer_ptr<engine::Renderer> renderer)
  : platform_(std::move(platform))
  , gfx_weak_(std::move(gfx_weak))
  , fullscreen_(fullscreen)
  , renderer_(renderer)
{
  DCHECK_NOTNULL_F(platform_);
  DCHECK_F(!gfx_weak_.expired());

  // Record start time for animations (use time_point for robust delta)
  start_time_ = std::chrono::steady_clock::now();
}

MainModule::~MainModule()
{
  LOG_SCOPE_F(INFO, "Destroying MainModule (AsyncEngine)");

  // Cleanup graphics resources
  if (!gfx_weak_.expired()) {
    const auto gfx = gfx_weak_.lock();
    const graphics::SingleQueueStrategy queues;
    if (auto queue
      = gfx->GetCommandQueue(queues.KeyFor(graphics::QueueRole::kGraphics))) {
      // queue->Flush(); // Commented out until we fix the interface
    }
  }

  framebuffers_.clear();
  renderer_.reset();
  surface_.reset();
  scene_.reset();
  platform_.reset();
}

auto MainModule::GetSupportedPhases() const noexcept -> engine::ModulePhaseMask
{
  using namespace core;
  return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
    PhaseId::kTransformPropagation, PhaseId::kFrameGraph,
    PhaseId::kCommandRecord, PhaseId::kFrameEnd>();
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  LOG_SCOPE_F(2, "MainModule::OnFrameStart");

  // Initialize on first frame
  if (!initialized_) {
    SetupMainWindow();
    SetupSurface();
    SetupRenderer();
    SetupShaders();
    initialized_ = true;
  }

  // Check if window is closed
  if (window_weak_.expired()) {
    // Window expired, reset surface
    LOG_F(WARNING, "Window expired, resetting surface");
    surface_.reset();
    return;
  }

  // Add our surface to the FrameContext every frame (part of module contract)
  // NOTE: FrameContext is recreated each frame, so we must populate it every
  // time
  DCHECK_NOTNULL_F(surface_);
  context.AddSurface(surface_);
  LOG_F(2, "Surface '{}' added to FrameContext for frame", surface_->GetName());

  // Ensure scene and camera are set up
  EnsureExampleScene();
  context.SetScene(observer_ptr { scene_.get() });
}

// Initialize a default looping flight path over the scene (few control points)
void MainModule::InitializeDefaultFlightPath()
{
  if (!camera_drone_.path_points.empty())
    return;

  camera_drone_.path_points.clear();
  camera_drone_.pois.clear();

  // Figure-eight (horizontal) path using a Gerono lemniscate pattern.
  // Produces a horizontal 8-loop at a fixed altitude that loops seamlessly.
  const int points = 96; // control polygon resolution
  const float a = 36.0f; // horizontal scale (half-width of loops)
  const float altitude = 14.0f; // fixed altitude for the 8-loop

  camera_drone_.path_points.reserve(points + 4);
  for (int i = 0; i < points; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(points);
    const float ang = t * glm::two_pi<float>();
    // Gerono lemniscate parameterization (horizontal figure-eight):
    // x = a * cos(ang), z = a * sin(ang) * cos(ang)
    const float x = a * std::cos(ang);
    const float z = a * std::sin(ang) * std::cos(ang);
    camera_drone_.path_points.push_back(glm::vec3(x, altitude, z));
  }

  // Do NOT append a duplicate closing point; EvalClosedCatmullRom already
  // wraps indices, and duplicating the first point can create seam artifacts.

  camera_drone_.path_length = ApproximatePathLength(camera_drone_.path_points);
  if (camera_drone_.path_length <= 0.0)
    camera_drone_.path_length = 1.0;
  camera_drone_.path_u = 0.0;
  camera_drone_.path_s = 0.0;

  // Build arc-length LUT for constant-speed traversal
  constexpr int kLutSamples = 512;
  BuildArcLengthLut(camera_drone_.path_points, kLutSamples,
    camera_drone_.arc_lut.u_samples, camera_drone_.arc_lut.s_samples);

  // Initialize drone current pose to the farthest/high start so the very
  // first frame reads as 'looking from far away' rather than snapping.
  if (!camera_drone_.path_points.empty()) {
    // Evaluate start from arc-length s to ensure consistency
    const double u0 = ArcLengthToParamU(camera_drone_.path_s,
      camera_drone_.arc_lut.u_samples, camera_drone_.arc_lut.s_samples);
    const glm::vec3 start = EvalClosedCatmullRom(camera_drone_.path_points, u0);
    camera_drone_.current_pos = start;

    // Compute tangent at start for forward-facing orientation and rotate it
    // toward the scene center by up to 45 degrees so the camera remains
    // forward-looking but as focused as possible on the scene.
    // Compute tangent using a small arc-length offset for numerical stability
    const double eps_s = camera_drone_.path_length * 1e-3;
    const double u_eps = ArcLengthToParamU(camera_drone_.path_s + eps_s,
      camera_drone_.arc_lut.u_samples, camera_drone_.arc_lut.s_samples);
    const glm::vec3 p_a
      = EvalClosedCatmullRom(camera_drone_.path_points, u_eps);
    glm::vec3 tangent = glm::normalize(p_a - start);
    if (glm::length(tangent) < 1e-6f)
      tangent = glm::vec3(0.0f, 0.0f, 1.0f);

    const glm::vec3 center = glm::vec3(0.0f, 2.5f, 0.0f);
    glm::vec3 to_center = center - camera_drone_.current_pos;
    if (glm::length(to_center) > 1e-6f)
      to_center = glm::normalize(to_center);
    else
      to_center = tangent;

    // Rotate tangent toward the center by at most 45 degrees
    auto RotateTowardByAngle
      = [](glm::vec3 from, glm::vec3 to, float max_angle) {
          const float eps_axis = 1e-6f;
          const float dotv = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
          const float ang = std::acos(dotv);
          // focus_strength controls how strongly we bias toward the target.
          const float focus_strength = 0.6f; // in [0,1]
          const float apply_angle = glm::min(max_angle, ang * focus_strength);
          glm::vec3 axis = glm::cross(from, to);
          if (glm::length(axis) < eps_axis) {
            // degenerate (parallel or anti-parallel): pick an arbitrary axis
            axis = glm::vec3(0.0f, 1.0f, 0.0f);
          } else {
            axis = glm::normalize(axis);
          }
          const glm::quat q = glm::angleAxis(apply_angle, axis);
          return glm::normalize(q * from);
        };

    // For the very first frame, force an exact look-at toward the scene
    // focal point so we never start looking away from the scene (this can
    // trigger renderer-side assumptions when starting far away).
    glm::vec3 init_fwd = to_center; // exact look-at on initialization
    camera_drone_.current_rot
      = glm::quatLookAtRH(init_fwd, glm::vec3(0.0f, 1.0f, 0.0f));
    camera_drone_.initialized = true; // avoid snapping on first Update
  }
}

auto MainModule::UpdateCameraDrone(double delta_time) -> void
{
  // Temporary toggle to disable drone flight for stutter diagnostics.
  // Set to false to restore the flight behavior.
  static constexpr bool kDisableDroneFlight = false;
  if (kDisableDroneFlight) {
    SetupFixedCamera(main_camera_);
    return;
  }

  auto& d = camera_drone_;
  if (!d.enabled) {
    static bool initialized = false;
    if (!initialized) {
      SetupFixedCamera(main_camera_);
      initialized = true;
    }
    return;
  }

  // Simple clamp for delta to avoid large jumps
  const double dt = std::min(delta_time, 0.05);

  // If no path, keep a fixed camera
  if (d.path_points.empty()) {
    SetupFixedCamera(main_camera_);
    return;
  }

  // Advance along the path by distance (arc-length) for constant speed
  if (d.path_length <= 0.0)
    d.path_length = 1.0;
  d.path_s = std::fmod(d.path_s + d.path_speed * dt, d.path_length);
  if (d.path_s < 0.0)
    d.path_s += d.path_length;
  const double u
    = ArcLengthToParamU(d.path_s, d.arc_lut.u_samples, d.arc_lut.s_samples);

  // Sample position and tangent
  // Sample position and compute tangent using small arc-length offset
  const glm::vec3 p = EvalClosedCatmullRom(d.path_points, u);
  const double eps_s = d.path_length * 1e-3; // ~0.1% of path length
  const double u_eps = ArcLengthToParamU(
    d.path_s + eps_s, d.arc_lut.u_samples, d.arc_lut.s_samples);
  const glm::vec3 p_a = EvalClosedCatmullRom(d.path_points, u_eps);
  glm::vec3 tangent = p_a - p;
  if (glm::length(tangent) > 1e-6f)
    tangent = glm::normalize(tangent);
  else
    tangent = glm::vec3(0.0f, 0.0f, 1.0f);

  const glm::vec3 cam_pos = p;

  // Compute a forward vector biased toward the scene focal point but within
  // rotation constraints (max 45 degrees). Keep camera primarily forward.
  const glm::vec3 focus_target(static_cast<float>(d.focus_offset.x),
    d.focus_height, static_cast<float>(d.focus_offset.y));
  glm::vec3 focus_dir = focus_target - cam_pos;
  if (glm::length(focus_dir) > 1e-6f)
    focus_dir = glm::normalize(focus_dir);
  else
    focus_dir = tangent;

  const float max_rot = glm::radians(180.0f);
  const float focus_strength = 0.8f; // how strongly to bias toward focus
  const float dotv = glm::clamp(glm::dot(tangent, focus_dir), -1.0f, 1.0f);
  const float ang = std::acos(dotv);
  const float apply_angle = glm::min(max_rot, ang * focus_strength);
  glm::vec3 axis = glm::cross(tangent, focus_dir);
  if (glm::length(axis) < 1e-6f)
    axis = glm::vec3(0.0f, 1.0f, 0.0f);
  else
    axis = glm::normalize(axis);
  const glm::quat rot = glm::angleAxis(apply_angle, axis);
  glm::vec3 final_fwd = glm::normalize(rot * tangent);

  // Clamp pitch to +/-45 degrees
  auto ClampForwardPitch = [](glm::vec3 fwd) {
    const float max_pitch = glm::radians(45.0f);
    glm::vec3 horiz = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));
    if (glm::length(horiz) < 1e-6f)
      return fwd;
    const float current_pitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
    if (current_pitch > max_pitch) {
      const float y = std::sin(max_pitch);
      const float scale = std::cos(max_pitch);
      return glm::normalize(glm::vec3(horiz.x * scale, y, horiz.z * scale));
    } else if (current_pitch < -max_pitch) {
      const float y = std::sin(-max_pitch);
      const float scale = std::cos(-max_pitch);
      return glm::normalize(glm::vec3(horiz.x * scale, y, horiz.z * scale));
    }
    return fwd;
  };
  final_fwd = ClampForwardPitch(final_fwd);

  const glm::vec3 base_up(0.0f, 1.0f, 0.0f);
  const glm::quat desired_rot = glm::quatLookAtRH(final_fwd, base_up);

  // Simple smoothing for position and rotation
  const float smooth_t
    = static_cast<float>(glm::clamp(1.0 - std::exp(-dt * d.damping), 0.0, 1.0));
  if (!d.initialized) {
    d.current_pos = cam_pos;
    d.current_rot = desired_rot;
    d.initialized = true;
  } else {
    d.current_pos = glm::mix(d.current_pos, cam_pos, smooth_t);
    d.current_rot = glm::slerp(d.current_rot, desired_rot, smooth_t);
  }

  main_camera_.GetTransform().SetLocalPosition(d.current_pos);
  main_camera_.GetTransform().SetLocalRotation(d.current_rot);
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnSceneMutation");
  if (!surface_ || window_weak_.expired()) {
    LOG_F(ERROR, "Window or Surface is no longer valid");
    surface_.reset();
    context.RemoveSurfaceAt(0); // FIXME: find our surface index
    co_return;
  }

  EnsureMainCamera(
    static_cast<int>(surface_->Width()), static_cast<int>(surface_->Height()));

  // FIXME: view management is temporary
  context.AddView(std::make_shared<renderer::CameraView>(
    renderer::CameraView::Params {
      .camera_node = main_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    },
    surface_));

  // Handle scene mutations (material overrides, visibility changes)
  // Use the engine-provided frame start time so all modules use a
  // consistent timestamp for this frame. This avoids micro-jitter caused
  // by sampling the clock at slightly different moments inside the frame
  // pipeline.
  const auto now = context.GetFrameStartTime();
  const float delta_time
    = std::chrono::duration<float>(now - start_time_).count();
  UpdateSceneMutations(delta_time);

  co_return;
}

auto MainModule::OnTransformPropagation(engine::FrameContext& context)
  -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnTransformPropagation");

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

  co_return;
}

auto MainModule::OnFrameGraph(engine::FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnFrameGraph");

  // Setup framebuffers if needed
  if (framebuffers_.empty()) {
    SetupFramebuffers();
  }

  // Setup render passes (frame graph configuration)
  SetupRenderPasses();

  co_return;
}

auto MainModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnCommandRecord");

  if (gfx_weak_.expired() || !scene_) {
    co_return;
  }

  // Execute the actual rendering commands
  co_await ExecuteRenderCommands(context);
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void
{
  LOG_SCOPE_F(2, "MainModule::OnFrameEnd");

  // In AsyncEngine, modules do NOT present surfaces directly.
  // The engine handles presentation in PhasePresent() by calling
  // gfx->PresentSurfaces() on surfaces marked as presentable.
  //
  // Module responsibilities:
  // 1. Add surfaces to FrameContext during OnFrameStart
  // 2. Mark surfaces presentable after OnCommandRecord
  // 3. Engine handles actual presentation
  //
  // NOTE: FrameContext is recreated each frame, so surface registration and
  // presentable flags are fresh for each frame.

  LOG_F(2, "Frame end - surface presentation handled by AsyncEngine");
}

auto MainModule::SetupMainWindow() -> void
{
  // Set up the main window
  WindowProps props("Oxygen Graphics Demo - AsyncEngine");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = fullscreen_,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  window_weak_ = platform_->Windows().MakeWindow(props);
  if (const auto window = window_weak_.lock()) {
    LOG_F(INFO, "Main window {} is created", window->Id());
  }
}

auto MainModule::SetupSurface() -> void
{
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(!window_weak_.expired());

  const auto gfx = gfx_weak_.lock();

  auto queue = gfx->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (!queue) {
    LOG_F(ERROR, "No graphics command queue available to create surface");
    throw std::runtime_error("No graphics command queue available");
  }
  surface_ = gfx->CreateSurface(window_weak_, queue);
  surface_->SetName("Main Window Surface (AsyncEngine)");
  LOG_F(INFO, "Surface ({}) created for main window ({})", surface_->GetName(),
    window_weak_.lock()->Id());
}

auto MainModule::SetupRenderer() -> void
{
  CHECK_NOTNULL_F(renderer_, "Renderer was not provided to MainModule");
  LOG_F(INFO, "Using provided Renderer for AsyncEngine");
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(surface_ != nullptr, "Surface must be created before framebuffers");
  auto gfx = gfx_weak_.lock();

  // Get actual surface dimensions (important for full-screen mode)
  const auto surface_width = surface_->Width();
  const auto surface_height = surface_->Height();

  framebuffers_.clear();
  for (auto i = 0U; i < frame::kFramesInFlight.get(); ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = surface_width;
    depth_desc.height = surface_height;
    depth_desc.format = Format::kDepth32;
    depth_desc.texture_type = TextureType::kTexture2D;
    depth_desc.is_shader_resource = true;
    depth_desc.is_render_target = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    depth_desc.initial_state = graphics::ResourceStates::kDepthWrite;
    const auto depth_tex = gfx->CreateTexture(depth_desc);

    auto desc = graphics::FramebufferDesc {}
                  .AddColorAttachment(surface_->GetBackBuffer(i))
                  .SetDepthAttachment(depth_tex);

    framebuffers_.push_back(gfx->CreateFramebuffer(desc));
    CHECK_NOTNULL_F(
      framebuffers_[i], "Failed to create framebuffer for main window");
  }
}

auto MainModule::SetupShaders() -> void
{
  CHECK_F(!gfx_weak_.expired());
  const auto gfx = gfx_weak_.lock();

  // Verify that the shaders can be loaded by the Graphics backend
  const auto vertex_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
    ShaderType::kVertex, "FullScreenTriangle.hlsl"));

  const auto pixel_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
    ShaderType::kPixel, "FullScreenTriangle.hlsl"));

  CHECK_NOTNULL_F(
    vertex_shader, "Failed to load FullScreenTriangle vertex shader");
  CHECK_NOTNULL_F(
    pixel_shader, "Failed to load FullScreenTriangle pixel shader");

  LOG_F(INFO, "Engine shaders loaded successfully");
}

auto MainModule::EnsureExampleScene() -> void
{
  if (scene_) {
    return;
  }

  using oxygen::scene::Scene;

  scene_ = std::make_shared<Scene>("ExampleScene");

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
    auto node = scene_->CreateNode(name.c_str());
    node.GetRenderable().SetGeometry(sphere_geo);

    // Enlarge sphere to better showcase transparency layering against
    // background
    if (node.IsAlive()) {
      node.GetTransform().SetLocalScale(glm::vec3(3.0F));
    }

    // Configure LOD policy per-sphere (disabled during diagnostics)
    if (!kDisableSphereLodPolicy) {
      if (auto obj = node.GetObject()) {
        auto& r = obj->get()
                    .GetComponent<oxygen::scene::detail::RenderableComponent>();
        DistancePolicy pol;
        pol.thresholds = { 6.2f }; // switch LOD0->1 around ~6.2
        pol.hysteresis_ratio = 0.08f; // modest hysteresis to avoid flicker
        r.SetLodPolicy(std::move(pol));
      }
    }

    // Randomized parameters: seed ensures reproducible runs
    const double two_pi = static_cast<double>(glm::two_pi<float>());
    const double base_phase
      = (two_pi * static_cast<double>(i)) / static_cast<double>(kNumSpheres);
    const double jitter = phase_jitter(rng);
    const double init_angle = base_phase + jitter;
    const double speed = speed_dist(rng);
    const double radius = radius_dist(rng);
    const double hue = hue_dist(rng);

    // Apply per-sphere material override (transparent glass-like)
    if (auto obj = node.GetObject()) {
      auto& r
        = obj->get().GetComponent<oxygen::scene::detail::RenderableComponent>();
      const std::string mat_name
        = std::string("SphereMat_") + std::to_string(i);
      const auto rgb = ColorFromHue(hue);
      const bool is_transparent
        = kForceOpaqueSpheres ? false : (transp_dist(rng) < 0.5);
      const float alpha = is_transparent ? 0.35f : 1.0f;
      const auto domain = is_transparent
        ? oxygen::data::MaterialDomain::kAlphaBlended
        : oxygen::data::MaterialDomain::kOpaque;
      const glm::vec4 color(static_cast<float>(rgb.x),
        static_cast<float>(rgb.y), static_cast<float>(rgb.z), alpha);
      const auto mat = MakeSolidColorMaterial(mat_name.c_str(), color, domain);
      // Apply override for submesh index 0 across all LODs so switching LOD
      // retains the material override. Use EffectiveLodCount() to iterate.
      const auto lod_count = r.EffectiveLodCount();
      for (std::size_t lod = 0; lod < lod_count; ++lod) {
        r.SetMaterialOverride(lod, 0, mat);
      }
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
  multisubmesh_ = scene_->CreateNode("MultiSubmesh");
  multisubmesh_.GetRenderable().SetGeometry(quad2sm_geo);
  multisubmesh_.GetTransform().SetLocalPosition(glm::vec3(0.0F));
  multisubmesh_.GetTransform().SetLocalRotation(glm::quat(1, 0, 0, 0));
  // Set up a default flight path for the camera drone
  InitializeDefaultFlightPath();

  LOG_F(
    INFO, "Scene created: SphereDistance (LOD) and MultiSubmesh (per-submesh)");
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  using oxygen::scene::PerspectiveCamera;
  using oxygen::scene::camera::ProjectionConvention;

  if (!scene_) {
    return;
  }

  if (!main_camera_.IsAlive()) {
    main_camera_ = scene_->CreateNode("MainCamera");
  }

  if (!main_camera_.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
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
    cam.SetViewport(oxygen::ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }
}

auto MainModule::UpdateAnimations(double delta_time) -> void
{
  // delta_time is the elapsed time since last frame in seconds (double).
  // Clamp large deltas to avoid jumps after pause/hitch (50 ms max)
  constexpr double kMaxDelta = 0.05;
  const double effective_dt = (delta_time > kMaxDelta) ? kMaxDelta : delta_time;

  const double two_pi = static_cast<double>(glm::two_pi<float>());

  // Absolute-time sampling for deterministic, jitter-free animation
  anim_time_ += effective_dt;
  for (auto& s : spheres_) {
    const double angle = std::fmod(s.base_angle + s.speed * anim_time_, two_pi);
    const double spin
      = std::fmod(s.base_spin_angle + s.spin_speed * anim_time_, two_pi);
    AnimateSphereOrbit(s.node, angle, s.radius, s.inclination, spin);
  }

  // Periodic lightweight logging to inspect very small deltas (avoid spam)
  static int dbg_counter = 0;
  ++dbg_counter;
  if ((dbg_counter % 120) == 0) {
    LOG_F(INFO, "[Anim] delta_time={}ms spheres={}", delta_time * 1000.0,
      static_cast<int>(spheres_.size()));
  }

  // Camera update (drone) - encapsulated in helper
  if (main_camera_.IsAlive()) {
    UpdateCameraDrone(effective_dt);
  }
}

auto MainModule::UpdateSceneMutations(const float delta_time) -> void
{
  // Toggle per-submesh visibility and material override over time
  if (multisubmesh_.IsAlive()) {
    if (auto obj = multisubmesh_.GetObject()) {
      auto& r = obj->get().GetComponent<scene::detail::RenderableComponent>();
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
          desc.base_color[0] = 0.2f;
          desc.base_color[1] = 0.3f;
          desc.base_color[2] = 1.0f;
          desc.base_color[3] = 1.0f;
          auto blue = std::make_shared<const data::MaterialAsset>(
            desc, std::vector<data::ShaderReference> {});
          r.SetMaterialOverride(lod, 1, blue);
        } else {
          r.ClearMaterialOverride(lod, 1);
        }
        LOG_F(INFO, "[MultiSubmesh] Submesh 1 override -> {}",
          apply_override ? "blue" : "clear");
      }
    }
  }
}

auto MainModule::SetupRenderPasses() -> void
{
  LOG_SCOPE_F(2, "MainModule::SetupRenderPasses");

  // --- DepthPrePass configuration ---
  if (!depth_pass_config_) {
    depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config_->debug_name = "DepthPrePass";
  }
  if (!depth_pass_) {
    depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  }

  // --- ShaderPass configuration ---
  if (!shader_pass_config_) {
    shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config_->clear_color
      = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F }; // Custom clear color
    shader_pass_config_->debug_name = "ShaderPass";
  }
  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  // --- TransparentPass configuration ---
  if (!transparent_pass_config_) {
    transparent_pass_config_
      = std::make_shared<engine::TransparentPass::Config>();
    transparent_pass_config_->debug_name = "TransparentPass";
  }
  // Color/depth textures are assigned each frame just before execution (in
  // ExecuteRenderCommands)
  if (!transparent_pass_) {
    transparent_pass_
      = std::make_shared<engine::TransparentPass>(transparent_pass_config_);
  }
}

auto MainModule::ExecuteRenderCommands(engine::FrameContext& context)
  -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::ExecuteRenderCommands");

  if (gfx_weak_.expired() || !scene_) {
    co_return;
  }

  auto gfx = gfx_weak_.lock();

  // Use frame slot provided by the engine context
  const auto current_frame = context.GetFrameSlot().get();

  DLOG_F(1, "Recording commands for frame index {}", current_frame);

  auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "Main Window Command List");

  if (!recorder) {
    LOG_F(ERROR, "Failed to acquire command recorder");
    co_return;
  }

  // Always render to the framebuffer that wraps the swapchain's current
  // backbuffer. The swapchain's backbuffer index may not match the engine's
  // frame slot due to resize or present timing; querying the surface avoids
  // D3D12 validation errors (WRONGSWAPCHAINBUFFERREFERENCE).
  const auto backbuffer_index = surface_->GetCurrentBackBufferIndex();
  const auto fb = framebuffers_.at(backbuffer_index);
  fb->PrepareForRender(*recorder);
  recorder->BindFrameBuffer(*fb);

  // Create render context for renderer
  engine::RenderContext render_context;
  render_context.framebuffer = fb;

  // Execute render graph using the configured passes
  co_await renderer_->ExecuteRenderGraph(
    [&](const engine::RenderContext& context) -> co::Co<> {
      // Depth Pre-Pass execution
      if (depth_pass_) {
        co_await depth_pass_->PrepareResources(context, *recorder);
        co_await depth_pass_->Execute(context, *recorder);
      }
      // Shader Pass execution
      if (shader_pass_) {
        co_await shader_pass_->PrepareResources(context, *recorder);
        co_await shader_pass_->Execute(context, *recorder);
      }
      // Transparent Pass execution (reuses color/depth from framebuffer)
      if (transparent_pass_) {
        // Assign attachments each frame (framebuffer back buffer + depth)
        if (fb && !framebuffers_.empty()) {
          // Color: back buffer texture for current frame
          transparent_pass_config_->color_texture
            = fb->GetDescriptor().color_attachments[0].texture;
          // Depth: same depth texture used by depth pass
          if (fb->GetDescriptor().depth_attachment.IsValid()) {
            transparent_pass_config_->depth_texture
              = fb->GetDescriptor().depth_attachment.texture;
          }
        }
        co_await transparent_pass_->PrepareResources(context, *recorder);
        co_await transparent_pass_->Execute(context, *recorder);
      }
    },
    render_context, context);

  LOG_F(2, "Command recording completed for frame {}", current_frame);

  co_return;
}
