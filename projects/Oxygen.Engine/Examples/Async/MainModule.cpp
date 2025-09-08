//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <chrono>
#include <cstdint>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

  // Semi-transparent material (transparent domain) with lower alpha to
  // accentuate blending against background.
  const auto glass = MakeSolidColorMaterial("Glass",
    { 0.2f, 0.6f, 0.9f, 0.35f }, oxygen::data::MaterialDomain::kAlphaBlended);

  // LOD 0: higher tessellation
  auto lod0_data = oxygen::data::MakeSphereMeshAsset(32, 64);
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

  // LOD 1: lower tessellation
  auto lod1_data = oxygen::data::MakeSphereMeshAsset(12, 24);
  CHECK_F(lod1_data.has_value());
  auto mesh1
    = MeshBuilder(1, "SphereLOD1")
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

  // Use LOD0 bounds for asset bounds
  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 2;
  const glm::vec3 bb_min = mesh0->BoundingBoxMin();
  const glm::vec3 bb_max = mesh0->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

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

// Orbit sphere around origin on XZ plane.
auto AnimateSphereOrbit(oxygen::scene::SceneNode& sphere_node, float t) -> void
{
  constexpr float radius = 4.0F;
  constexpr float angular_speed = 0.6F; // radians per second
  const float angle = angular_speed * t;
  const glm::vec3 pos(radius * std::cos(angle), 0.0F, radius * std::sin(angle));
  if (sphere_node.IsAlive()) {
    sphere_node.GetTransform().SetLocalPosition(pos);
  }
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
    SetupCommandQueues();
    SetupMainWindow();
    SetupSurface();
    SetupRenderer();
    SetupShaders();
    initialized_ = true;

    // Record start time for animations
    const auto now = std::chrono::steady_clock::now();
    const auto epoch = now.time_since_epoch();
    start_time_ = std::chrono::duration<float>(epoch).count();
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

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnSceneMutation");
  try {
    if (surface_) {
      EnsureMainCamera(static_cast<int>(surface_->Width()),
        static_cast<int>(surface_->Height()));
    }
  } catch (const std::exception& ex) {
    if (window_weak_.expired()) {
      LOG_F(ERROR, "window is no longer valid: {}", ex.what());
      surface_.reset();
      context.RemoveSurfaceAt(0); // FIXME: find our surface index
    }
    co_return;
  }

  // Handle scene mutations (material overrides, visibility changes)
  const auto now = std::chrono::steady_clock::now();
  const auto epoch = now.time_since_epoch();
  const float current_time = std::chrono::duration<float>(epoch).count();
  UpdateSceneMutations(current_time - start_time_);

  co_return;
}

auto MainModule::OnTransformPropagation(engine::FrameContext& context)
  -> co::Co<>
{
  LOG_SCOPE_F(2, "MainModule::OnTransformPropagation");

  // Update animations and transforms (no scene mutations)
  const auto now = std::chrono::steady_clock::now();
  const auto epoch = now.time_since_epoch();
  const float current_time = std::chrono::duration<float>(epoch).count();
  UpdateAnimations(current_time - start_time_);

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

  // Build the frame in the renderer
  if (renderer_ && scene_) {
    engine::CameraView::Params cv {
      .camera_node = main_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    };
    renderer_->BuildFrame(engine::CameraView(cv), context);
  }

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

  // Mark our surface as presentable after rendering is complete
  // This is part of the module contract - surfaces must be marked presentable
  // before the Present phase. Since FrameContext is recreated each frame,
  // we need to find and mark our surface every frame.
  if (surface_) {
    auto surfaces = context.GetSurfaces();
    for (size_t i = 0; i < surfaces.size(); ++i) {
      if (surfaces[i] == surface_) {
        context.SetSurfacePresentable(i, true);
        LOG_F(2, "Surface '{}' marked as presentable at index {}",
          surface_->GetName(), i);
        break;
      }
    }
  }
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

auto MainModule::SetupCommandQueues() -> void
{
  CHECK_F(!gfx_weak_.expired());

  const auto gfx = gfx_weak_.lock();
  gfx->CreateCommandQueues(graphics::SingleQueueStrategy());
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

  const graphics::SingleQueueStrategy queues;
  surface_ = gfx->CreateSurface(window_weak_,
    gfx->GetCommandQueue(queues.KeyFor(graphics::QueueRole::kGraphics)));
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

  // Sphere with distance-based LOD; initial position will be set by orbit
  sphere_distance_ = scene_->CreateNode("SphereDistance");
  sphere_distance_.GetRenderable().SetGeometry(sphere_geo);

  // Enlarge sphere to better showcase transparency layering against background
  if (sphere_distance_.IsAlive()) {
    sphere_distance_.GetTransform().SetLocalScale(glm::vec3(3.0F));
  }

  // Configure LOD policy
  if (auto obj = sphere_distance_.GetObject()) {
    auto& r
      = obj->get().GetComponent<oxygen::scene::detail::RenderableComponent>();
    DistancePolicy pol;
    pol.thresholds = { 6.2f }; // switch LOD0->1 around ~6.2
    pol.hysteresis_ratio = 0.08f; // modest hysteresis to avoid flicker
    r.SetLodPolicy(std::move(pol));
  }

  // Multi-submesh quad centered at origin facing +Z (already in XY plane)
  multisubmesh_ = scene_->CreateNode("MultiSubmesh");
  multisubmesh_.GetRenderable().SetGeometry(quad2sm_geo);
  multisubmesh_.GetTransform().SetLocalPosition(glm::vec3(0.0F));
  multisubmesh_.GetTransform().SetLocalRotation(glm::quat(1, 0, 0, 0));

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

auto MainModule::UpdateAnimations(const float time_seconds) -> void
{
  // Sphere orbit
  AnimateSphereOrbit(sphere_distance_, time_seconds);
  // Fixed camera (set once when created; ensure orientation remains stable)
  if (main_camera_.IsAlive()) {
    static bool initialized = false;
    if (!initialized) {
      SetupFixedCamera(main_camera_);
      initialized = true;
    }
  }
}

auto MainModule::UpdateSceneMutations(const float time_seconds) -> void
{
  // Toggle per-submesh visibility and material override over time
  if (multisubmesh_.IsAlive()) {
    if (auto obj = multisubmesh_.GetObject()) {
      auto& r = obj->get().GetComponent<scene::detail::RenderableComponent>();
      constexpr std::size_t lod = 0;

      // Every 2 seconds, toggle submesh 0 visibility
      int vis_phase = static_cast<int>(time_seconds) / 2;
      if (vis_phase != last_vis_toggle_) {
        last_vis_toggle_ = vis_phase;
        const bool visible = (vis_phase % 2) == 0;
        r.SetSubmeshVisible(lod, 0, visible);
        LOG_F(INFO, "[MultiSubmesh] Submesh 0 visibility -> {}", visible);
      }

      // Every second, toggle an override on submesh 1 (use blue instead of
      // green)
      int ovr_phase = static_cast<int>(time_seconds);
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

  const graphics::SingleQueueStrategy queues;
  auto queue_key = queues.KeyFor(graphics::QueueRole::kGraphics);
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

  LOG_F(INFO, "Command recording completed for frame {}", current_frame);

  co_return;
}
