//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <type_traits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Include first to avoid conflicts between winsock2(asio) and windows.h
#include <Oxygen/Platform/Platform.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

#include "./MainModule.h"

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::data::Mesh;
using oxygen::data::Vertex;
using oxygen::engine::RenderItem;
using oxygen::graphics::Buffer;
using oxygen::graphics::Framebuffer;
using oxygen::scene::DistancePolicy;

namespace {
constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;
} // namespace

namespace {
// Centralize example scene state across frames.
struct ExampleState {
  std::shared_ptr<oxygen::scene::Scene> scene;
  // Nodes demonstrating features
  oxygen::scene::SceneNode sphere_distance; // LOD policy: Distance
  oxygen::scene::SceneNode multisubmesh; // Per-submesh visibility/overrides
  oxygen::scene::SceneNode main_camera; // "MainCamera"
};

ExampleState g_state;

// Build a 2-LOD sphere GeometryAsset (high and low tessellation)
auto BuildSphereLodAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  // LOD 0: higher tessellation
  auto lod0_data = oxygen::data::MakeSphereMeshAsset(32, 64);
  CHECK_F(lod0_data.has_value());
  auto mesh0
    = MeshBuilder(0, "SphereLOD0")
        .WithVertices(lod0_data->first)
        .WithIndices(lod0_data->second)
        .BeginSubMesh("full", MaterialAsset::CreateDefault())
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
        .BeginSubMesh("full", MaterialAsset::CreateDefault())
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

// Build a 1-LOD mesh with two submeshes (two triangles of a quad)
auto BuildTwoSubmeshQuadAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  // Helper: make a solid-color material asset snapshot
  auto MakeSolidColorMaterial
    = [](const char* name,
        const glm::vec4& rgba) -> std::shared_ptr<const MaterialAsset> {
    oxygen::data::pak::MaterialAssetDesc desc {};
    desc.header.asset_type = 7; // MaterialAsset (for tooling/debug)
    // Safe copy name
    constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
    const std::size_t n = (std::min)(maxn, std::strlen(name));
    std::memcpy(desc.header.name, name, n);
    desc.header.name[n] = '\0';
    desc.header.version = 1;
    desc.header.streaming_priority = 255;
    desc.material_domain
      = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
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
      desc, std::vector<oxygen::data::ShaderReference> {});
  };

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
                // Submesh 0: first triangle
                .BeginSubMesh("tri0", red)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                // Submesh 1: second triangle
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

// Ensure the example scene and two cubes exist; store persistent handles.
auto EnsureExampleScene() -> void
{
  if (g_state.scene) {
    return;
  }

  using oxygen::data::GenerateMesh;
  using oxygen::data::GeometryAsset;
  using oxygen::data::Mesh;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::scene::Scene;

  g_state.scene = std::make_shared<Scene>("ExampleScene");
  // Create a LOD sphere and a multi-submesh quad
  auto sphere_geo = BuildSphereLodAsset();
  auto quad2sm_geo = BuildTwoSubmeshQuadAsset();

  // Sphere with distance-based LOD at origin
  g_state.sphere_distance = g_state.scene->CreateNode("SphereDistance");
  g_state.sphere_distance.GetRenderable().SetGeometry(sphere_geo);
  // Configure LOD policy via component access
  if (auto obj = g_state.sphere_distance.GetObject()) {
    auto& r
      = obj->get().GetComponent<oxygen::scene::detail::RenderableComponent>();
    DistancePolicy pol;
    // With the current camera orbit (radius ~5–7), set the distance
    // threshold near the orbit so we can observe LOD flips in wireframe.
    // For 2 LODs, only the first threshold is used.
    pol.thresholds = { 6.2f }; // switch LOD0->1 around ~6.2
    pol.hysteresis_ratio = 0.08f; // modest hysteresis to avoid flicker
    r.SetLodPolicy(std::move(pol));
  }

  // Multi-submesh quad offset on +X
  g_state.multisubmesh = g_state.scene->CreateNode("MultiSubmesh");
  g_state.multisubmesh.GetRenderable().SetGeometry(quad2sm_geo);
  g_state.multisubmesh.GetTransform().SetLocalPosition(
    glm::vec3(3.0F, 0.0F, 0.0F));

  LOG_F(
    INFO, "Scene created: SphereDistance (LOD) and MultiSubmesh (per-submesh)");
}

// Find or create the "MainCamera" node with a PerspectiveCamera; keep aspect in
// sync.
auto EnsureMainCamera(const int width, const int height) -> void
{
  using oxygen::scene::PerspectiveCamera;
  using oxygen::scene::camera::ProjectionConvention;

  if (!g_state.scene) {
    return;
  }

  if (!g_state.main_camera.IsAlive()) {
    g_state.main_camera = g_state.scene->CreateNode("MainCamera");
  }

  if (!g_state.main_camera.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
    const bool attached = g_state.main_camera.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  // Configure camera params (aspect from current surface size)
  const auto cam_ref = g_state.main_camera.GetCameraAs<PerspectiveCamera>();
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

// Update the MainCamera transform to look from position toward target with up.
auto UpdateMainCameraPose(const glm::vec3& position, const glm::vec3& target,
  const glm::vec3& up) -> void
{
  if (!g_state.main_camera.IsAlive()) {
    return;
  }

  auto t = g_state.main_camera.GetTransform();
  t.SetLocalPosition(position);

  // Build a rotation that looks at target. Use RH variant to match glm::lookAt.
  const glm::vec3 dir = glm::normalize(target - position);
  const glm::quat rot = glm::quatLookAtRH(dir, up);
  t.SetLocalRotation(rot);
}

// Animate the main camera along a smooth orbit/dolly path around the scene
// center with subtle height and radius modulation for a cinematic feel.
auto AnimateMainCamera(const float time_seconds) -> void
{
  // Scene center between the two cubes (kept consistent with setup)
  constexpr glm::vec3 center(1.25F, 0.0F, 0.0F);

  // Base parameters
  constexpr float base_radius = 6.0F;
  constexpr float base_height = 1.6F;

  // Modulations for a more cinematic motion
  const float radius
    = base_radius + 1.25F * std::sin(0.35F * time_seconds); // slow dolly
  const float height
    = base_height + 0.45F * std::sin(0.8F * time_seconds + 0.7F); // bob
  constexpr float angular_speed = 0.35F; // radians/sec (slow orbit)
  const float angle = angular_speed * time_seconds;

  // Orbit around center; keep negative Z bias to face the scene as in setup
  const glm::vec3 offset(
    radius * std::cos(angle), height, -radius * std::sin(angle));
  const glm::vec3 position = center + offset;
  constexpr glm::vec3 target = center;
  constexpr glm::vec3 up(0.0F, 1.0F, 0.0F);

  UpdateMainCameraPose(position, target, up);
}

} // namespace

MainModule::MainModule(
  std::shared_ptr<Platform> platform, std::weak_ptr<Graphics> gfx_weak)
  : platform_(std::move(platform))
  , gfx_weak_(std::move(gfx_weak))
{
  DCHECK_NOTNULL_F(platform_);
  DCHECK_F(!gfx_weak_.expired());
}

MainModule::~MainModule()
{
  LOG_SCOPE_F(INFO, "Destroying MainModule");

  // Flush command queues used for the surface
  if (!gfx_weak_.expired()) {
    const auto gfx = gfx_weak_.lock();
    const graphics::SingleQueueStrategy queues;
    gfx->GetCommandQueue(queues.KeyFor(graphics::QueueRole::kGraphics))
      ->Flush();
  }

  // Un-register the vertex buffer view if it's valid
  // (No need to release descriptor handle, ResourceRegistry manages it)
  if (render_controller_ && renderer_) {
    try {
      auto& registry = render_controller_->GetResourceRegistry();
      const auto items = renderer_->GetOpaqueItems();
      if (!items.empty() && items.front().mesh) {
        auto vertex_buffer = renderer_->GetVertexBuffer(*items.front().mesh);
        registry.UnRegisterViews(*vertex_buffer);
      }
    } catch (const std::exception& e) {
      LOG_F(
        ERROR, "Error while un-registering vertex buffer view: {}", e.what());
    }
  }

  context_.framebuffer.reset(); // Do not hold onto the framebuffer
  framebuffers_.clear();
  surface_->DetachRenderer();
  renderer_.reset();
  render_controller_.reset();
  surface_.reset();
  platform_.reset();
}

auto MainModule::Run() -> void
{
  DCHECK_NOTNULL_F(nursery_);
  SetupCommandQueues();
  SetupMainWindow();
  SetupSurface();
  SetupRenderer();
  SetupShaders();
  surface_->AttachRenderer(render_controller_);

  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired() && !gfx_weak_.expired()) {
      const auto gfx = gfx_weak_.lock();
      co_await gfx->OnRenderStart();
      // Submit the render task to the renderer
      render_controller_->Submit(
        [this]() -> co::Co<> { co_await RenderScene(); });
    }
  });
}

auto MainModule::SetupCommandQueues() const -> void
{
  CHECK_F(!gfx_weak_.expired());

  const auto gfx = gfx_weak_.lock();
  gfx->CreateCommandQueues(graphics::SingleQueueStrategy());
}

auto MainModule::SetupSurface() -> void
{
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(!window_weak_.expired());

  const auto gfx = gfx_weak_.lock();

  const graphics::SingleQueueStrategy queues;
  surface_ = gfx->CreateSurface(window_weak_,
    gfx->GetCommandQueue(queues.KeyFor(graphics::QueueRole::kGraphics)));
  surface_->SetName("Main Window Surface");
  LOG_F(INFO, "Surface ({}) created for main window ({})", surface_->GetName(),
    window_weak_.lock()->Id());
}

auto MainModule::SetupMainWindow() -> void
{
  // Set up the main window
  WindowProps props("Oxygen Graphics Example");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = false,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  window_weak_ = platform_->Windows().MakeWindow(props);
  if (const auto window = window_weak_.lock()) {
    LOG_F(INFO, "Main window {} is created", window->Id());
  }

  // Immediately accept the close request for the main window
  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired()) {
      const auto window = window_weak_.lock();
      co_await window->CloseRequested();
      // Stop the nursery
      if (nursery_) {
        nursery_->Cancel();
      }
      window_weak_.lock()->VoteToClose();
    }
  });

  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired()) {
      const auto window = window_weak_.lock();
      if (const auto [from, to] = co_await window->Events().UntilChanged();
        to == WindowEvent::kResized) {
        LOG_F(INFO, "Main window was resized");
        surface_->ShouldResize(true);
        framebuffers_.clear();
      } else {
        if (to == WindowEvent::kExposed) {
          LOG_F(INFO, "My window is exposed");
        }
      }
    }
  });

  // Add a termination signal handler
  nursery_->Start([this]() -> co::Co<> {
    co_await platform_->Async().OnTerminate();
    LOG_F(INFO, "terminating...");
    // Terminate the application by requesting the main window to close
    window_weak_.lock()->RequestClose();
  });
}

auto MainModule::SetupRenderer() -> void
{
  CHECK_F(!gfx_weak_.expired());

  const auto gfx = gfx_weak_.lock();
  render_controller_ = gfx->CreateRenderController(
    "Main Window Renderer", surface_, frame::kFramesInFlight);
  CHECK_NOTNULL_F(
    render_controller_, "Failed to create renderer for main window");
  renderer_ = std::make_shared<engine::Renderer>(render_controller_);
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  auto gfx = gfx_weak_.lock();

  for (auto i = 0U; i < frame::kFramesInFlight.get(); ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = kWindowWidth;
    depth_desc.height = kWindowHeight;
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

auto MainModule::SetupShaders() const -> void
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

// Phase 2: SRVs and indices are ensured in Renderer::EnsureMeshResources.

// === Data-driven RenderItem/scene system integration ===

auto MainModule::RenderScene() -> co::Co<>
{
  if (gfx_weak_.expired()) {
    co_return;
  }

  if (framebuffers_.empty()) {
    SetupFramebuffers();
  }

  DLOG_F(1, "Rendering scene in frame index {}",
    render_controller_->CurrentFrameIndex());

  // Ensure example scene content and camera exist
  EnsureExampleScene();
  EnsureMainCamera(
    static_cast<int>(surface_->Width()), static_cast<int>(surface_->Height()));

  auto gfx = gfx_weak_.lock();
  const auto recorder = render_controller_->AcquireCommandRecorder(
    graphics::SingleQueueStrategy().KeyFor(graphics::QueueRole::kGraphics),
    "Main Window Command List");

  const auto fb = framebuffers_[render_controller_->CurrentFrameIndex().get()];
  fb->PrepareForRender(*recorder);
  recorder->BindFrameBuffer(*fb);

  context_.framebuffer = fb;

  // --- DepthPrePass integration ---
  static std::shared_ptr<engine::DepthPrePass> depth_pass;
  static std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config;
  if (!depth_pass_config) {
    depth_pass_config = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config->debug_name = "ShaderPass";
  }
  if (!depth_pass) {
    depth_pass = std::make_shared<engine::DepthPrePass>(depth_pass_config);
  }

  // --- ShaderPass integration ---
  static std::shared_ptr<engine::ShaderPass> shader_pass;
  static std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  if (!shader_pass_config) {
    shader_pass_config = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config->clear_color
      = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F }; // Custom clear color
    shader_pass_config->debug_name = "ShaderPass";
  }
  if (!shader_pass) {
    shader_pass = std::make_shared<engine::ShaderPass>(shader_pass_config);
  }

  // Animate rotation angle (for a bit of motion)
  static float rotation_angle = 0.0F; // radians
  rotation_angle += 0.01F; // radians per frame
  const auto rot_y
    = glm::angleAxis(rotation_angle, glm::vec3(0.0F, 1.0F, 0.0F));
  if (g_state.sphere_distance.IsAlive()) {
    g_state.sphere_distance.GetTransform().SetLocalRotation(rot_y);
  }
  if (g_state.multisubmesh.IsAlive()) {
    const auto rot = glm::angleAxis(
      -rotation_angle * 0.8F, glm::normalize(glm::vec3(0.0F, 1.0F, 0.25F)));
    g_state.multisubmesh.GetTransform().SetLocalRotation(rot);
  }

  // Animate camera using wall-clock elapsed time for smooth motion
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  const float t = std::chrono::duration<float>(clock::now() - t0).count();
  AnimateMainCamera(t);

  // Toggle per-submesh visibility and material override over time
  if (g_state.multisubmesh.IsAlive()) {
    // Access RenderableComponent component
    if (auto obj = g_state.multisubmesh.GetObject()) {
      auto& r = obj->get().GetComponent<scene::detail::RenderableComponent>();
      constexpr std::size_t lod = 0;
      // Every 2 seconds, toggle submesh 0 visibility
      static int last_vis_toggle = -1;
      int vis_phase = static_cast<int>(t) / 2;
      if (vis_phase != last_vis_toggle) {
        last_vis_toggle = vis_phase;
        const bool visible = (vis_phase % 2) == 0;
        r.SetSubmeshVisible(lod, 0, visible);
        LOG_F(INFO, "[MultiSubmesh] Submesh 0 visibility -> {}", visible);
      }
      // Every 3 seconds, toggle an override on submesh 1 (use blue instead of
      // debug)
      static int last_ovr_toggle = -1;
      int ovr_phase = static_cast<int>(t) / 3;
      if (ovr_phase != last_ovr_toggle) {
        last_ovr_toggle = ovr_phase;
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
          desc.base_color[0] = 0.1f;
          desc.base_color[1] = 0.1f;
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

  if (renderer_) {
    engine::CameraView::Params cv {
      .camera_node = g_state.main_camera,
      // Let camera ActiveViewport drive; we already keep camera viewport in
      // sync
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    };
    renderer_->BuildFrame(*g_state.scene, engine::CameraView(cv));
  }

  // Assemble and run the render graph
  co_await renderer_->ExecuteRenderGraph(
    [&](const engine::RenderContext& context) -> co::Co<> {
      // Depth Pre-Pass
      co_await depth_pass->PrepareResources(context, *recorder);
      co_await depth_pass->Execute(context, *recorder);
      // Shader Pass
      co_await shader_pass->PrepareResources(context, *recorder);
      co_await shader_pass->Execute(context, *recorder);
    },
    context_);

  co_return;
}
