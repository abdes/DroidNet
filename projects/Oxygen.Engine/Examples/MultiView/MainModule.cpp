//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <optional>
#include <system_error>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>

#include "../Common/RenderGraph.h"
#include "MainModule.h"

using namespace oxygen::examples::multiview;
using oxygen::Scissors;
using oxygen::ViewPort;

namespace {
constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;
constexpr float kPipWidthRatio = 0.33F;
constexpr float kPipHeightRatio = 0.33F;
constexpr float kPipMargin = 24.0F;

auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba)
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7;
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(MaterialDomain::kOpaque);
  desc.flags = 0;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0f;
  desc.metalness = 0.0f;
  desc.roughness = 0.5f;
  desc.ambient_occlusion = 1.0f;
  return std::make_shared<const MaterialAsset>(
    desc, std::vector<ShaderReference> {});
}

auto ComputePipExtent(const int surface_width, const int surface_height)
  -> std::pair<uint32_t, uint32_t>
{
  const auto pip_width = std::max(1,
    static_cast<int>(
      std::lround(static_cast<float>(surface_width) * kPipWidthRatio)));
  const auto pip_height = std::max(1,
    static_cast<int>(
      std::lround(static_cast<float>(surface_height) * kPipHeightRatio)));
  return { static_cast<uint32_t>(pip_width),
    static_cast<uint32_t>(pip_height) };
}

auto ComputePipViewport(const int surface_width, const int surface_height,
  const uint32_t pip_width, const uint32_t pip_height) -> ViewPort
{
  const float max_width = static_cast<float>(surface_width);
  const float width = static_cast<float>(pip_width);
  const float height = static_cast<float>(pip_height);

  const float offset_x = std::max(0.0F, max_width - width - kPipMargin);
  const float offset_y = kPipMargin;

  return ViewPort { .top_left_x = offset_x,
    .top_left_y = offset_y,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F };
}

} // namespace

MainModule::MainModule(const common::AsyncEngineApp& app) noexcept
  : Base(app)
  , app_(app)
{
  LOG_F(INFO, "MultiView example module created");
}

MainModule::~MainModule() = default;

auto MainModule::GetSupportedPhases() const noexcept
  -> oxygen::engine::ModulePhaseMask
{
  using namespace oxygen::core;
  return oxygen::engine::MakeModuleMask<PhaseId::kFrameStart,
    PhaseId::kSceneMutation, PhaseId::kFrameGraph, PhaseId::kCommandRecord>();
}

auto MainModule::BuildDefaultWindowProperties() const
  -> oxygen::platform::window::Properties
{
  oxygen::platform::window::Properties props("Oxygen - Multi-View Rendering");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = false,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  return props;
}

auto MainModule::OnExampleFrameStart(oxygen::engine::FrameContext& context)
  -> void
{
  bool drop_main_resources = false;
  bool drop_pip_resources = false;
  if (app_window_) {
    const bool app_window_resize = app_window_->ShouldResize();
    const auto surface = app_window_->GetSurface();
    const bool missing_surface = !surface;
    const bool surface_resize = surface && surface->ShouldResize();

    drop_main_resources = app_window_resize || surface_resize
      || (missing_surface
        && (static_cast<bool>(main_framebuffer_)
          || static_cast<bool>(main_color_texture_)
          || static_cast<bool>(main_depth_texture_)));

    drop_pip_resources = drop_main_resources
      || (missing_surface
        && (pip_framebuffer_ || pip_color_texture_ || pip_depth_texture_));
  } else {
    drop_main_resources = static_cast<bool>(main_framebuffer_)
      || static_cast<bool>(main_color_texture_)
      || static_cast<bool>(main_depth_texture_);

    drop_pip_resources = static_cast<bool>(pip_framebuffer_)
      || static_cast<bool>(pip_color_texture_)
      || static_cast<bool>(pip_depth_texture_);
  }

  if (drop_main_resources) {
    ReleaseMainViewResources();
  }

  if (drop_pip_resources) {
    ReleasePipViewResources();
  }

  EnsureScene();
  if (scene_) {
    context.SetScene(oxygen::observer_ptr { scene_.get() });
  }
}

auto MainModule::OnFrameGraph(oxygen::engine::FrameContext& /*context*/)
  -> oxygen::co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (app_window_->GetFramebuffers().empty()) {
    app_window_->EnsureFramebuffers();
  }

  if (render_graph_) {
    render_graph_->SetupRenderPasses();
  }
  co_return;
}

auto MainModule::ReleaseTexture(
  std::shared_ptr<oxygen::graphics::Texture>& texture) -> void
{
  if (!texture) {
    return;
  }

  const auto gfx = app_.gfx_weak.lock();
  if (gfx) {
    try {
      oxygen::graphics::DeferredObjectRelease(
        texture, gfx->GetDeferredReclaimer());
      return;
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "[MultiView] Failed to defer release for texture {}: {}",
        static_cast<const void*>(texture.get()), ex.what());
    }
  }

  texture.reset();
}

auto MainModule::ReleaseFramebuffer(
  std::shared_ptr<oxygen::graphics::Framebuffer>& framebuffer) -> void
{
  if (!framebuffer) {
    return;
  }

  const auto gfx = app_.gfx_weak.lock();
  if (gfx) {
    try {
      oxygen::graphics::DeferredObjectRelease(
        framebuffer, gfx->GetDeferredReclaimer());
      return;
    } catch (const std::exception& ex) {
      LOG_F(WARNING,
        "[MultiView] Failed to defer release for framebuffer {}: {}",
        static_cast<const void*>(framebuffer.get()), ex.what());
    }
  }

  framebuffer.reset();
}

auto MainModule::ReleaseMainViewResources() -> void
{
  if (!main_framebuffer_ && !main_color_texture_ && !main_depth_texture_) {
    return;
  }

  LOG_F(INFO, "[MultiView] Releasing main view targets");
  ReleaseFramebuffer(main_framebuffer_);
  ReleaseTexture(main_color_texture_);
  ReleaseTexture(main_depth_texture_);
}

auto MainModule::ReleasePipViewResources() -> void
{
  if (!pip_framebuffer_ && !pip_color_texture_ && !pip_depth_texture_) {
    return;
  }

  LOG_F(INFO, "[MultiView] Releasing PiP view targets");
  ReleaseFramebuffer(pip_framebuffer_);
  ReleaseTexture(pip_color_texture_);
  ReleaseTexture(pip_depth_texture_);
  pip_target_width_ = 0;
  pip_target_height_ = 0;
  pip_destination_viewport_.reset();
}

auto MainModule::EnsureScene() -> void
{
  if (scene_) {
    return;
  }
  scene_ = std::make_shared<oxygen::scene::Scene>("MultiViewScene");
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

  const auto cam_ref = main_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(45.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(100.0F);
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }

  main_camera_.GetTransform().SetLocalPosition({ 0.0f, 0.0f, 5.0f });
}

auto MainModule::EnsurePipCamera(const int pip_width, const int pip_height)
  -> void
{
  using oxygen::scene::PerspectiveCamera;
  using oxygen::scene::camera::ProjectionConvention;

  if (!scene_) {
    return;
  }

  if (!pip_camera_.IsAlive()) {
    pip_camera_ = scene_->CreateNode("PiPCamera");
  }

  if (!pip_camera_.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
    const bool attached = pip_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to PiPCamera");
  }

  const auto cam_ref = pip_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = pip_height > 0
      ? (static_cast<float>(pip_width) / static_cast<float>(pip_height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(35.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(100.0F);
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(pip_width),
      .height = static_cast<float>(pip_height),
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }

  pip_camera_.GetTransform().SetLocalPosition({ -3.0f, 2.5f, 4.0f });
  const auto rotation
    = glm::quat(glm::vec3(glm::radians(-20.0F), glm::radians(-110.0F), 0.0F));
  pip_camera_.GetTransform().SetLocalRotation(rotation);
}

auto MainModule::OnSceneMutation(oxygen::engine::FrameContext& context)
  -> oxygen::co::Co<>
{
  if (!app_window_) {
    co_return;
  }

  const auto surface = app_window_->GetSurface();
  if (!surface) {
    co_return;
  }

  const auto surface_width = static_cast<int>(surface->Width());
  const auto surface_height = static_cast<int>(surface->Height());

  // One-time scene setup
  if (!initialized_) {
    if (scene_ && !sphere_node_.IsAlive()) {
      auto sphere_geom_data = oxygen::data::MakeSphereMeshAsset(32, 32);
      if (sphere_geom_data.has_value()) {
        auto material = MakeSolidColorMaterial(
          "SphereMaterial", { 0.2f, 0.7f, 0.3f, 1.0f });

        using oxygen::data::MeshBuilder;
        using oxygen::data::pak::GeometryAssetDesc;
        using oxygen::data::pak::MeshViewDesc;

        auto mesh = MeshBuilder(0, "Sphere")
                      .WithVertices(sphere_geom_data->first)
                      .WithIndices(sphere_geom_data->second)
                      .BeginSubMesh("full", material)
                      .WithMeshView(MeshViewDesc {
                        .first_index = 0,
                        .index_count = static_cast<uint32_t>(
                          sphere_geom_data->second.size()),
                        .first_vertex = 0,
                        .vertex_count
                        = static_cast<uint32_t>(sphere_geom_data->first.size()),
                      })
                      .EndSubMesh()
                      .Build();

        GeometryAssetDesc geo_desc {};
        geo_desc.lod_count = 1;
        const glm::vec3 bb_min = mesh->BoundingBoxMin();
        const glm::vec3 bb_max = mesh->BoundingBoxMax();
        geo_desc.bounding_box_min[0] = bb_min.x;
        geo_desc.bounding_box_min[1] = bb_min.y;
        geo_desc.bounding_box_min[2] = bb_min.z;
        geo_desc.bounding_box_max[0] = bb_max.x;
        geo_desc.bounding_box_max[1] = bb_max.y;
        geo_desc.bounding_box_max[2] = bb_max.z;

        auto geom_asset = std::make_shared<oxygen::data::GeometryAsset>(
          geo_desc,
          std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });

        sphere_node_ = scene_->CreateNode("Sphere");
        sphere_node_.GetRenderable().SetGeometry(geom_asset);
        sphere_node_.GetTransform().SetLocalPosition({ 0.0f, 0.0f, -2.0f });

        LOG_F(INFO, "Scene created with sphere");
      }
    }

    initialized_ = true;
  }

  EnsureMainCamera(surface_width, surface_height);
  const auto [pip_width, pip_height]
    = ComputePipExtent(surface_width, surface_height);
  pip_target_width_ = pip_width;
  pip_target_height_ = pip_height;
  pip_destination_viewport_
    = ComputePipViewport(surface_width, surface_height, pip_width, pip_height);
  EnsurePipCamera(static_cast<int>(pip_width), static_cast<int>(pip_height));

  // Main view: full screen
  main_camera_view_ = std::make_shared<oxygen::renderer::CameraView>(
    oxygen::renderer::CameraView::Params {
      .camera_node = main_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    },
    surface);

  main_view_id_
    = context.AddView(oxygen::engine::ViewContext { .name = "MainView",
      .surface = *surface,
      .metadata = { .tag = "Main_Solid" } });

  pip_camera_view_ = std::make_shared<oxygen::renderer::CameraView>(
    oxygen::renderer::CameraView::Params {
      .camera_node = pip_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    },
    surface);

  pip_view_id_ = context.AddView(oxygen::engine::ViewContext {
    .name = "WireframePiP",
    .surface = *surface,
    .metadata = { .tag = "PiP_Wireframe" },
  });

  co_return;
}

auto MainModule::OnCommandRecord(oxygen::engine::FrameContext& context)
  -> oxygen::co::Co<>
{
  if (!initialized_ || !app_window_ || !render_graph_) {
    co_return;
  }

  const auto surface = app_window_->GetSurface();
  if (!surface) {
    co_return;
  }

  auto gfx = app_.gfx_weak.lock();
  if (!gfx) {
    co_return;
  }

  const auto framebuffers = app_window_->GetFramebuffers();
  const auto backbuffer_index = surface->GetCurrentBackBufferIndex();
  if (framebuffers.empty() || backbuffer_index >= framebuffers.size()) {
    co_return;
  }

  const auto fb = framebuffers.at(backbuffer_index);
  if (!fb) {
    co_return;
  }

  auto recorder = AcquireCommandRecorder(*gfx);
  if (!recorder) {
    LOG_F(ERROR, "Failed to acquire command recorder");
    co_return;
  }

  TrackSwapchainFramebuffer(*recorder, *fb);
  EnsureMainRenderTargets(*gfx, *surface, *fb, *recorder);
  EnsurePipRenderTargets(*gfx, *surface, *fb, *recorder);

  // Render scene into dedicated render targets, then composite to the
  // swapchain.
  co_await RenderMainViewOffscreen(context, *recorder);
  co_await RenderPipViewWireframe(context, *recorder);

  if (main_view_ready_) {
    CompositeMainViewToBackbuffer(context, *recorder, fb, *surface);
  } else {
    LOG_F(WARNING,
      "[MultiView] Skipping main view composite (renderer requested skip)");
  }

  if (pip_view_ready_) {
    CompositePipViewToBackbuffer(*recorder, fb, *surface);
  } else {
    LOG_F(
      WARNING, "[MultiView] Skipping PiP composite (renderer requested skip)");
  }
  MarkSurfacePresentable(context, surface);

  co_return;
}

auto MainModule::AcquireCommandRecorder(oxygen::Graphics& gfx)
  -> std::shared_ptr<graphics::CommandRecorder>
{
  const auto queue_key = gfx.QueueKeyFor(graphics::QueueRole::kGraphics);
  return gfx.AcquireCommandRecorder(queue_key, "MultiView Command List");
}

auto MainModule::TrackSwapchainFramebuffer(graphics::CommandRecorder& recorder,
  const graphics::Framebuffer& framebuffer) -> void
{
  const auto& fb_desc = framebuffer.GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      recorder.BeginTrackingResourceState(
        *attachment.texture, oxygen::graphics::ResourceStates::kPresent);
    }
  }

  if (fb_desc.depth_attachment.texture) {
    recorder.BeginTrackingResourceState(*fb_desc.depth_attachment.texture,
      oxygen::graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }
}

auto MainModule::EnsureMainRenderTargets(oxygen::Graphics& gfx,
  const graphics::Surface& surface, const graphics::Framebuffer& framebuffer,
  graphics::CommandRecorder& recorder) -> void
{
  const bool needs_recreate = !main_color_texture_
    || main_color_texture_->GetDescriptor().width != surface.Width()
    || main_color_texture_->GetDescriptor().height != surface.Height();

  if (needs_recreate) {
    ReleaseMainViewResources();
    LOG_F(INFO,
      "[MultiView] (Re)creating main textures: prev_color={}, prev_depth={}, "
      "surface={}x{}",
      static_cast<bool>(main_color_texture_),
      static_cast<bool>(main_depth_texture_), surface.Width(),
      surface.Height());

    const auto& swapchain_desc = framebuffer.GetDescriptor();

    oxygen::graphics::TextureDesc color_desc;
    color_desc.width = surface.Width();
    color_desc.height = surface.Height();
    color_desc.format
      = swapchain_desc.color_attachments[0].texture->GetDescriptor().format;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.debug_name = "Main Color Texture";
    color_desc.texture_type = oxygen::TextureType::kTexture2D;
    color_desc.mip_levels = 1;
    color_desc.array_size = 1;
    color_desc.sample_count = 1;
    color_desc.depth = 1;
    color_desc.use_clear_value = true;
    color_desc.clear_value
      = oxygen::graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
    main_color_texture_ = gfx.CreateTexture(color_desc);
    if (main_color_texture_) {
      const auto& d = main_color_texture_->GetDescriptor();
      LOG_F(INFO,
        "[MultiView] Created Main Color Texture: ptr={}, size={}x{}, "
        "format={}, rt={}, srv={}",
        static_cast<const void*>(main_color_texture_.get()), d.width, d.height,
        static_cast<int>(d.format), d.is_render_target, d.is_shader_resource);
    } else {
      LOG_F(ERROR, "[MultiView] Failed to create Main Color Texture");
    }

    oxygen::graphics::TextureDesc depth_desc {};
    if (swapchain_desc.depth_attachment.IsValid()
      && swapchain_desc.depth_attachment.texture) {
      depth_desc = swapchain_desc.depth_attachment.texture->GetDescriptor();
      depth_desc.debug_name = "Main Depth Texture";
    } else {
      depth_desc.width = surface.Width();
      depth_desc.height = surface.Height();
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.texture_type = oxygen::TextureType::kTexture2D;
      depth_desc.mip_levels = 1;
      depth_desc.array_size = 1;
      depth_desc.sample_count = 1;
      depth_desc.depth = 1;
      depth_desc.is_render_target = false;
      depth_desc.is_shader_resource = false;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value
        = oxygen::graphics::Color { 1.0f, 0.0f, 0.0f, 0.0f };
      depth_desc.debug_name = "Main Depth Texture";
    }
    main_depth_texture_ = gfx.CreateTexture(depth_desc);

    if (main_depth_texture_) {
      const auto& d = main_depth_texture_->GetDescriptor();
      LOG_F(INFO,
        "[MultiView] Created Main Depth Texture: ptr={}, size={}x{}, "
        "format={}, rt={}, srv={}, use_clear={}, clear=({}, {}, {}, {})",
        static_cast<const void*>(main_depth_texture_.get()), d.width, d.height,
        static_cast<int>(d.format), d.is_render_target, d.is_shader_resource,
        d.use_clear_value, d.clear_value.r, d.clear_value.g, d.clear_value.b,
        d.clear_value.a);
    } else {
      LOG_F(ERROR, "[MultiView] Failed to create Main Depth Texture");
    }

    // NOTE: The framebuffer will register the textures we pass to it and take
    // ownership of them until destroyed.
    oxygen::graphics::FramebufferDesc main_desc;
    main_desc.AddColorAttachment({ .texture = main_color_texture_,
      .sub_resources = oxygen::graphics::TextureSubResourceSet::EntireTexture(),
      .format = main_color_texture_->GetDescriptor().format });
    main_desc.depth_attachment.texture = main_depth_texture_;
    main_desc.depth_attachment.sub_resources
      = oxygen::graphics::TextureSubResourceSet {};
    main_framebuffer_ = gfx.CreateFramebuffer(main_desc);

    if (main_framebuffer_) {
      LOG_F(INFO,
        "[MultiView] Created Main Framebuffer: ptr={} color_tex={} "
        "depth_tex={}",
        static_cast<const void*>(main_framebuffer_.get()),
        static_cast<const void*>(main_color_texture_.get()),
        static_cast<const void*>(main_depth_texture_.get()));
    } else {
      LOG_F(ERROR, "[MultiView] Failed to create Main Framebuffer");
    }
  }

  if (main_color_texture_) {
    recorder.BeginTrackingResourceState(
      *main_color_texture_, oxygen::graphics::ResourceStates::kCommon);
  }

  if (main_depth_texture_) {
    recorder.BeginTrackingResourceState(
      *main_depth_texture_, oxygen::graphics::ResourceStates::kDepthWrite);
  }
}

auto MainModule::EnsurePipRenderTargets(oxygen::Graphics& gfx,
  const graphics::Surface& surface, const graphics::Framebuffer& framebuffer,
  graphics::CommandRecorder& recorder) -> void
{
  uint32_t pip_width = pip_target_width_;
  uint32_t pip_height = pip_target_height_;

  if (pip_width == 0 || pip_height == 0) {
    const auto [computed_width, computed_height] = ComputePipExtent(
      static_cast<int>(surface.Width()), static_cast<int>(surface.Height()));
    pip_width = pip_target_width_ = computed_width;
    pip_height = pip_target_height_ = computed_height;
    pip_destination_viewport_
      = ComputePipViewport(static_cast<int>(surface.Width()),
        static_cast<int>(surface.Height()), pip_width, pip_height);
  }

  const bool needs_recreate = !pip_color_texture_
    || pip_color_texture_->GetDescriptor().width != pip_width
    || pip_color_texture_->GetDescriptor().height != pip_height;

  if (needs_recreate) {
    ReleasePipViewResources();
    pip_target_width_ = pip_width;
    pip_target_height_ = pip_height;
    pip_destination_viewport_
      = ComputePipViewport(static_cast<int>(surface.Width()),
        static_cast<int>(surface.Height()), pip_width, pip_height);
    LOG_F(INFO,
      "[MultiView] (Re)creating PiP textures: prev_color={}, prev_depth={}, "
      "pip={}x{}",
      static_cast<bool>(pip_color_texture_),
      static_cast<bool>(pip_depth_texture_), pip_width, pip_height);

    const auto& swapchain_desc = framebuffer.GetDescriptor();

    oxygen::graphics::TextureDesc color_desc;
    color_desc.width = pip_width;
    color_desc.height = pip_height;
    color_desc.format
      = swapchain_desc.color_attachments[0].texture->GetDescriptor().format;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.debug_name = "PiP Color Texture";
    color_desc.texture_type = oxygen::TextureType::kTexture2D;
    color_desc.mip_levels = 1;
    color_desc.array_size = 1;
    color_desc.sample_count = 1;
    color_desc.depth = 1;
    color_desc.use_clear_value = true;
    color_desc.clear_value
      = oxygen::graphics::Color { 0.05F, 0.05F, 0.05F, 1.0F };
    pip_color_texture_ = gfx.CreateTexture(color_desc);
    if (pip_color_texture_) {
      const auto& d = pip_color_texture_->GetDescriptor();
      LOG_F(INFO,
        "[MultiView] Created PiP Color Texture: ptr={}, size={}x{}, format={}"
        ", rt={}, srv={}",
        static_cast<const void*>(pip_color_texture_.get()), d.width, d.height,
        static_cast<int>(d.format), d.is_render_target, d.is_shader_resource);
    }

    oxygen::graphics::TextureDesc depth_desc {};
    if (swapchain_desc.depth_attachment.IsValid()
      && swapchain_desc.depth_attachment.texture) {
      depth_desc = swapchain_desc.depth_attachment.texture->GetDescriptor();
      depth_desc.width = pip_width;
      depth_desc.height = pip_height;
      depth_desc.debug_name = "PiP Depth Texture";
    } else {
      depth_desc.width = pip_width;
      depth_desc.height = pip_height;
      depth_desc.format = oxygen::Format::kDepth32;
      depth_desc.texture_type = oxygen::TextureType::kTexture2D;
      depth_desc.mip_levels = 1;
      depth_desc.array_size = 1;
      depth_desc.sample_count = 1;
      depth_desc.depth = 1;
      depth_desc.is_render_target = false;
      depth_desc.is_shader_resource = false;
      depth_desc.use_clear_value = true;
      depth_desc.clear_value
        = oxygen::graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
      depth_desc.debug_name = "PiP Depth Texture";
    }
    pip_depth_texture_ = gfx.CreateTexture(depth_desc);
    if (pip_depth_texture_) {
      const auto& d = pip_depth_texture_->GetDescriptor();
      LOG_F(INFO,
        "[MultiView] Created PiP Depth Texture: ptr={}, size={}x{}, format={}"
        ", rt={}, srv={}, use_clear={}, clear=({}, {}, {}, {})",
        static_cast<const void*>(pip_depth_texture_.get()), d.width, d.height,
        static_cast<int>(d.format), d.is_render_target, d.is_shader_resource,
        d.use_clear_value, d.clear_value.r, d.clear_value.g, d.clear_value.b,
        d.clear_value.a);
    }

    oxygen::graphics::FramebufferDesc pip_desc;
    pip_desc.AddColorAttachment({ .texture = pip_color_texture_,
      .sub_resources = oxygen::graphics::TextureSubResourceSet::EntireTexture(),
      .format = pip_color_texture_->GetDescriptor().format });
    pip_desc.depth_attachment.texture = pip_depth_texture_;
    pip_desc.depth_attachment.sub_resources
      = oxygen::graphics::TextureSubResourceSet {};
    pip_framebuffer_ = gfx.CreateFramebuffer(pip_desc);
  }

  if (pip_color_texture_) {
    recorder.BeginTrackingResourceState(
      *pip_color_texture_, oxygen::graphics::ResourceStates::kCommon);
  }

  if (pip_depth_texture_) {
    recorder.BeginTrackingResourceState(
      *pip_depth_texture_, oxygen::graphics::ResourceStates::kDepthWrite);
  }
}

auto MainModule::RenderMainViewOffscreen(oxygen::engine::FrameContext& context,
  graphics::CommandRecorder& recorder) -> oxygen::co::Co<>
{
  main_view_ready_ = false;
  if (!render_graph_ || !main_framebuffer_ || !main_camera_view_) {
    co_return;
  }

  const auto& main_fb_desc = main_framebuffer_->GetDescriptor();
  for (const auto& att : main_fb_desc.color_attachments) {
    if (att.texture) {
      recorder.RequireResourceState(
        *att.texture, oxygen::graphics::ResourceStates::kRenderTarget);
    }
  }
  if (main_fb_desc.depth_attachment.texture) {
    recorder.RequireResourceState(*main_fb_desc.depth_attachment.texture,
      oxygen::graphics::ResourceStates::kDepthWrite);
  }

  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*main_framebuffer_);
  render_graph_->PrepareForRenderFrame(main_framebuffer_);

  auto& shader_config = render_graph_->GetShaderPassConfig();
  if (shader_config) {
    shader_config->clear_color
      = oxygen::graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
    shader_config->should_clear = true;
  }

  try {
    const auto view = main_camera_view_->Resolve();
    app_.renderer->PrepareView(main_view_id_, view, context);

    co_await app_.renderer->RenderView(
      main_view_id_,
      [&](const oxygen::engine::RenderContext& render_context)
        -> oxygen::co::Co<> {
        co_await render_graph_->RunPasses(render_context, recorder);
      },
      render_graph_->GetRenderContext(), context);

    main_view_ready_ = true;
  } catch (const std::system_error& ex) {
    LOG_F(WARNING, "[MultiView] Renderer skipped main view this frame: {}",
      ex.what());
  }
}

auto MainModule::RenderPipViewWireframe(oxygen::engine::FrameContext& context,
  graphics::CommandRecorder& recorder) -> oxygen::co::Co<>
{
  pip_view_ready_ = false;
  if (!render_graph_ || !pip_framebuffer_ || !pip_camera_view_) {
    co_return;
  }

  const auto& pip_fb_desc = pip_framebuffer_->GetDescriptor();
  for (const auto& att : pip_fb_desc.color_attachments) {
    if (att.texture) {
      recorder.RequireResourceState(
        *att.texture, oxygen::graphics::ResourceStates::kRenderTarget);
    }
  }
  if (pip_fb_desc.depth_attachment.texture) {
    recorder.RequireResourceState(*pip_fb_desc.depth_attachment.texture,
      oxygen::graphics::ResourceStates::kDepthWrite);
  }

  recorder.FlushBarriers();
  recorder.BindFrameBuffer(*pip_framebuffer_);
  render_graph_->PrepareForWireframeRenderFrame(pip_framebuffer_);

  auto& shader_config = render_graph_->GetWireframeShaderPassConfig();
  if (shader_config) {
    shader_config->clear_color
      = oxygen::graphics::Color { 0.05F, 0.05F, 0.05F, 1.0F };
    shader_config->should_clear = true;
  }

  try {
    const auto view = pip_camera_view_->Resolve();
    app_.renderer->PrepareView(pip_view_id_, view, context);

    co_await app_.renderer->RenderView(
      pip_view_id_,
      [&](const oxygen::engine::RenderContext& render_context)
        -> oxygen::co::Co<> {
        co_await render_graph_->RunWireframePasses(render_context, recorder);
      },
      render_graph_->GetRenderContext(), context);

    context.SetViewOutput(pip_view_id_, pip_framebuffer_);
    pip_view_ready_ = true;
  } catch (const std::system_error& ex) {
    LOG_F(WARNING, "[MultiView] Renderer skipped PiP view this frame: {}",
      ex.what());
  }
}

auto MainModule::CompositeMainViewToBackbuffer(
  oxygen::engine::FrameContext& context, graphics::CommandRecorder& recorder,
  const std::shared_ptr<graphics::Framebuffer>& framebuffer,
  const graphics::Surface& surface) -> void
{
  if (!framebuffer || !main_color_texture_ || !main_depth_texture_) {
    return;
  }

  const auto& backbuffer
    = framebuffer->GetDescriptor().color_attachments[0].texture;
  if (!backbuffer) {
    return;
  }

  recorder.RequireResourceState(
    *main_color_texture_, oxygen::graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *backbuffer, oxygen::graphics::ResourceStates::kCopyDest);

  oxygen::graphics::TextureSlice full_slice;
  full_slice.width = surface.Width();
  full_slice.height = surface.Height();
  full_slice.depth = 1;

  recorder.FlushBarriers();
  recorder.CopyTexture(*main_color_texture_, full_slice,
    oxygen::graphics::TextureSubResourceSet::EntireTexture(), *backbuffer,
    full_slice, oxygen::graphics::TextureSubResourceSet::EntireTexture());

  recorder.RequireResourceState(
    *backbuffer, oxygen::graphics::ResourceStates::kPresent);
  context.SetViewOutput(main_view_id_, framebuffer);

  recorder.RequireResourceState(
    *main_color_texture_, oxygen::graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *main_depth_texture_, oxygen::graphics::ResourceStates::kDepthWrite);
}

auto MainModule::CompositePipViewToBackbuffer(
  graphics::CommandRecorder& recorder,
  const std::shared_ptr<graphics::Framebuffer>& framebuffer,
  const graphics::Surface& surface) -> void
{
  if (!framebuffer || !pip_color_texture_) {
    return;
  }

  const auto& backbuffer
    = framebuffer->GetDescriptor().color_attachments[0].texture;
  if (!backbuffer) {
    return;
  }

  recorder.RequireResourceState(
    *pip_color_texture_, oxygen::graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    *backbuffer, oxygen::graphics::ResourceStates::kCopyDest);

  oxygen::graphics::TextureSlice src_slice;
  src_slice.x = 0;
  src_slice.y = 0;
  src_slice.width = pip_color_texture_->GetDescriptor().width;
  src_slice.height = pip_color_texture_->GetDescriptor().height;
  src_slice.depth = 1;

  auto destination_viewport = pip_destination_viewport_;
  if (!destination_viewport.has_value()) {
    const auto [pip_width, pip_height] = ComputePipExtent(
      static_cast<int>(surface.Width()), static_cast<int>(surface.Height()));
    destination_viewport = ComputePipViewport(static_cast<int>(surface.Width()),
      static_cast<int>(surface.Height()), pip_width, pip_height);
  }

  oxygen::graphics::TextureSlice dest_slice;
  dest_slice.x
    = static_cast<uint32_t>(std::lround(destination_viewport->top_left_x));
  dest_slice.y
    = static_cast<uint32_t>(std::lround(destination_viewport->top_left_y));
  dest_slice.width
    = static_cast<uint32_t>(std::lround(destination_viewport->width));
  dest_slice.height
    = static_cast<uint32_t>(std::lround(destination_viewport->height));
  dest_slice.depth = 1;

  recorder.FlushBarriers();
  recorder.CopyTexture(*pip_color_texture_, src_slice,
    oxygen::graphics::TextureSubResourceSet::EntireTexture(), *backbuffer,
    dest_slice, oxygen::graphics::TextureSubResourceSet::EntireTexture());

  recorder.RequireResourceState(
    *pip_color_texture_, oxygen::graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    *backbuffer, oxygen::graphics::ResourceStates::kPresent);
}

auto MainModule::MarkSurfacePresentable(oxygen::engine::FrameContext& context,
  const std::shared_ptr<graphics::Surface>& surface) -> void
{
  if (!surface) {
    return;
  }

  const auto surfaces = context.GetSurfaces();
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i] == surface) {
      context.SetSurfacePresentable(i, true);
      break;
    }
  }
}
