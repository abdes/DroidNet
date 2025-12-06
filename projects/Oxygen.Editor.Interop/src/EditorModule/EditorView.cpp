//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EditorModule/EditorView.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "EditorModule/ViewRenderer.h"

namespace oxygen::interop::module
{

  auto EditorView::Config::ResolveExtent() const -> SubPixelExtent
  {
    if (compositing_target.has_value() && compositing_target.value())
    {
      auto *surface = compositing_target.value();
      float w = static_cast<float>(surface->Width());
      float h = static_cast<float>(surface->Height());

      // Try to get more accurate dimensions from backbuffer if available
      auto back = surface->GetCurrentBackBuffer();
      if (back)
      {
        const auto &desc = back->GetDescriptor();
        if (desc.width > 0 && desc.height > 0)
        {
          w = static_cast<float>(desc.width);
          h = static_cast<float>(desc.height);
        }
      }
      return {.width = w, .height = h};
    }

    // Warn about misconfigured views using default 1x1 dimensions
    if (width == 1 && height == 1)
    {
      LOG_F(WARNING, "View '{}' has no compositing target and is using default 1x1 dimensions. "
                     "This likely indicates a misconfigured view.",
            name);
    }

    return {.width = static_cast<float>(width), .height = static_cast<float>(height)};
  }

  EditorView::EditorView(Config config)
      : config_(std::move(config))
  {
    renderer_ = std::make_unique<ViewRenderer>();

    // Initialize dimensions from config
    auto extent = config_.ResolveExtent();
    width_ = extent.width;
    height_ = extent.height;
  }

  EditorView::~EditorView()
  {
    if (state_ != ViewState::kDestroyed)
    {
      ReleaseResources();
    }
  }

  void EditorView::SetRenderingContext(const EditorViewContext &ctx)
  {
    current_context_ = &ctx;
    graphics_ = ctx.graphics.weak_from_this();
  }

  void EditorView::ClearPhaseRecorder()
  {
    current_context_ = nullptr;
  }

  void EditorView::Initialize(scene::Scene &scene)
  {
    if (state_ != ViewState::kCreating)
    {
      return;
    }

    // Store scene reference for later use
    scene_ = scene.weak_from_this();

    state_ = ViewState::kReady;
    LOG_F(INFO, "EditorView '{}' initialized.", config_.name);
  }

  void EditorView::OnSceneMutation()
  {
    LOG_SCOPE_F(4, "EditorView::OnSceneMutation");
    if (!current_context_ || state_ == ViewState::kDestroyed)
    {
      return;
    }

    if (!visible_)
    {
      return;
    }

    // Get scene for mutations
    auto scn = scene_.lock();
    if (!scn)
    {
      return;
    }

    // Create camera if this is the first time
    if (!camera_node_.IsAlive())
    {
      CreateCamera(*scn);
    }

    // Update camera for this frame
    UpdateCameraForFrame();

    // Register with FrameContext
    engine::ViewContext vc{
        .view = View{}, // Default view config
        .metadata = engine::ViewMetadata{.name = config_.name, .purpose = config_.purpose},
        .output = nullptr // Set later by Renderer or used internally
    };

    // We must never register views from EditorView.
    // The owning manager (ViewManager or higher-level module) is responsible
    // for registering views with FrameContext and assigning the engine ViewId.
    // If we don't have an assigned id yet it indicates a lifecycle error; log
    // and skip updating/registration here.
    if (view_id_ != kInvalidViewId)
    {
      current_context_->frame_context.UpdateView(view_id_, std::move(vc));
    }
    else
    {
      LOG_F(WARNING, "EditorView::OnSceneMutation invoked but EditorView has no engine-assigned ViewId. Owner must register the view before scenes are mutated.");
    }
  }

  auto EditorView::OnPreRender(engine::Renderer &renderer) -> oxygen::co::Co<>
  {
    LOG_SCOPE_F(4, "EditorView::OnPreRender");
    if (state_ != ViewState::kReady || !visible_)
    {
      co_return;
    }

    if (width_ <= 0.0f || height_ <= 0.0f)
    {
      co_return;
    }

    bool need_resize = false;
    if (!color_texture_ ||
        static_cast<float>(color_texture_->GetDescriptor().width) != width_ ||
        static_cast<float>(color_texture_->GetDescriptor().height) != height_)
    {
      need_resize = true;
    }

    if (need_resize)
    {
      if (auto gfx = graphics_.lock())
      {
        auto &reclaimer = gfx->GetDeferredReclaimer();
        if (color_texture_)
          graphics::DeferredObjectRelease(color_texture_, reclaimer);
        if (depth_texture_)
          graphics::DeferredObjectRelease(depth_texture_, reclaimer);
        if (framebuffer_)
          graphics::DeferredObjectRelease(framebuffer_, reclaimer);

        graphics::TextureDesc color_desc;
        color_desc.width = static_cast<uint32_t>(width_);
        color_desc.height = static_cast<uint32_t>(height_);
        color_desc.format = oxygen::Format::kRGBA8UNorm;
        color_desc.texture_type = oxygen::TextureType::kTexture2D;
        color_desc.is_render_target = true;
        color_desc.is_shader_resource = true;
        color_desc.initial_state = oxygen::graphics::ResourceStates::kShaderResource;

        // Assign a helpful debug name so runtime diagnostics show which
        // EditorView owns this texture (helps avoid generic "Texture" labels).
        std::string dbg_base = config_.name.empty() ? std::string("EditorView:Unnamed") : fmt::format("EditorView:{}", config_.name);
        color_desc.debug_name = dbg_base + ".Color";
        color_texture_ = gfx->CreateTexture(color_desc);

        graphics::TextureDesc depth_desc = color_desc;
        depth_desc.format = oxygen::Format::kDepth32;
        depth_desc.is_shader_resource = false;
        depth_desc.use_clear_value = true;
        depth_desc.clear_value = {1.0f, 0.0f, 0.0f, 0.0f};
        depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

        depth_desc.debug_name = dbg_base + ".Depth";
        depth_texture_ = gfx->CreateTexture(depth_desc);

        graphics::FramebufferDesc fb_desc;
        fb_desc.AddColorAttachment(color_texture_);
        fb_desc.SetDepthAttachment(depth_texture_);

        framebuffer_ = gfx->CreateFramebuffer(fb_desc);

        LOG_F(INFO, "EditorView '{}' resized resources to {}x{}", config_.name, width_, height_);
        // We'll set the FrameContext output for this view below (after resizing)
        // so that setting is handled once in a single place regardless of
        // whether we created new resources this frame or not.
      }
    }

    // Set initial camera orientation (only once, after transform propagation)
    if (!initial_orientation_set_ && camera_node_.IsAlive())
    {
      camera_node_.GetTransform().LookAt(glm::vec3(0.0f));
      initial_orientation_set_ = true;
    }

    // After resizing (or if resources previously existed) ensure the
    // FrameContext gets a single SetViewOutput call so Renderer can find
    // our framebuffer. This centralizes the SetViewOutput call — preventing
    // duplicate updates from both the creation and the renderer-path.
    if (framebuffer_ && view_id_ != kInvalidViewId && current_context_)
    {
      try
      {
        current_context_->frame_context.SetViewOutput(view_id_, oxygen::observer_ptr<graphics::Framebuffer>(framebuffer_.get()));
        // Ensure FrameContext now has the output populated
        DCHECK_NOTNULL_F(current_context_->frame_context.GetViewContext(view_id_).output,
                         "EditorView::OnPreRender - framebuffer did not populate FrameContext output for view {}",
                         view_id_);
      }
      catch (...)
      {
        DLOG_F(WARNING, "EditorView::OnPreRender - failed to SetViewOutput for framebuffer view {}", view_id_.get());
      }
    }

    // Update ViewRenderer with new framebuffer
    if (renderer_)
    {
      renderer_->SetFramebuffer(framebuffer_);

      // Ensure registered with renderer
      RegisterWithRenderer(renderer);
    }

    co_return;
  }

  void EditorView::Show()
  {
    visible_ = true;
  }

  void EditorView::Hide()
  {
    visible_ = false;
  }

  void EditorView::ReleaseResources()
  {
    if (state_ == ViewState::kDestroyed)
    {
      return;
    }

    state_ = ViewState::kReleasing;

    // Schedule GPU resources for deferred destruction
    if (auto gfx = graphics_.lock())
    {
      auto &reclaimer = gfx->GetDeferredReclaimer();
      if (color_texture_)
        graphics::DeferredObjectRelease(color_texture_, reclaimer);
      if (depth_texture_)
        graphics::DeferredObjectRelease(depth_texture_, reclaimer);
      if (framebuffer_)
        graphics::DeferredObjectRelease(framebuffer_, reclaimer);
    }

    // Unregister from engine if we have the renderer
    if (renderer_ && renderer_module_)
    {
      renderer_->UnregisterFromEngine(*renderer_module_);
    }
    renderer_module_ = nullptr;

    // Detach camera and destroy node
    if (camera_node_.IsAlive())
    {
      camera_node_.DetachCamera();
      if (auto scn = scene_.lock())
      {
        scn->DestroyNode(camera_node_);
      }
    }

    view_id_ = ViewId{};
    state_ = ViewState::kDestroyed;
  }

  void EditorView::CreateCamera(scene::Scene &scene)
  {
    // Create camera node in the scene
    camera_node_ = scene.CreateNode(config_.name + "_Camera");

    auto camera = std::make_unique<scene::PerspectiveCamera>(
        scene::camera::ProjectionConvention::kD3D12);
    camera_node_.AttachCamera(std::move(camera));

    // Set initial position (orientation setup happens in OnPreRender)
    camera_node_.GetTransform().SetLocalPosition(glm::vec3(0.0f, 0.0f, -10.0f));

    LOG_F(INFO, "EditorView '{}' created camera node", config_.name);
  }

  void EditorView::UpdateCameraForFrame()
  {
    if (!current_context_)
    {
      return;
    }

    // Update camera aspect ratio and viewport
    if (camera_node_.IsAlive())
    {
      auto cam_ref = camera_node_.GetCameraAs<scene::PerspectiveCamera>();
      if (cam_ref)
      {
        const float aspect = (width_ > 0.0f && height_ > 0.0f) ? (width_ / height_) : 1.0f;
        auto &cam = cam_ref->get();
        cam.SetAspectRatio(aspect);
        cam.SetViewport(ViewPort{
            .top_left_x = 0.0f,
            .top_left_y = 0.0f,
            .width = width_,
            .height = height_,
            .min_depth = 0.0f,
            .max_depth = 1.0f});
      }
    }
  }

  auto EditorView::GetViewId() const -> ViewId
  {
    return view_id_;
  }

  auto EditorView::GetState() const -> ViewState
  {
    return state_;
  }

  auto EditorView::IsVisible() const -> bool
  {
    return visible_;
  }

  auto EditorView::GetCameraNode() const -> scene::SceneNode
  {
    return camera_node_;
  }

  void EditorView::RegisterWithRenderer(engine::Renderer &renderer)
  {
    if (view_id_ == kInvalidViewId)
    {
      return;
    }

    // Store renderer for cleanup
    renderer_module_ = oxygen::observer_ptr<engine::Renderer>(&renderer);

    if (renderer_)
    {
      // Create resolver
      scene::SceneNode node = camera_node_;
      oxygen::engine::ViewResolver resolver = [node](const oxygen::engine::ViewContext &ctx) -> oxygen::ResolvedView
      {
        renderer::SceneCameraViewResolver scene_resolver([node](const ViewId &)
                                                         { return node; });
        return scene_resolver(ctx.id);
      };

      renderer_->RegisterWithEngine(renderer, view_id_, std::move(resolver));
    }
  }

  void EditorView::UnregisterFromRenderer(engine::Renderer &renderer)
  {
    if (renderer_)
    {
      renderer_->UnregisterFromEngine(renderer);
    }
  }

  void EditorView::SetRenderGraph(
      std::shared_ptr<engine::Renderer::RenderGraphFactory> factory)
  {
    render_graph_factory_ = std::move(factory);
  }

} // namespace oxygen::interop::module
