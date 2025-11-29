//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Core/Types/Frame.h>

#include "Unmanaged/EditorModule.h"
#include "Unmanaged/RenderGraph.h"

namespace Oxygen::Editor::EngineInterface {

  using namespace oxygen;
  using namespace oxygen::renderer;

  EditorModule::EditorModule(std::shared_ptr<SurfaceRegistry> registry)
    : registry_(std::move(registry)) {
    if (registry_ == nullptr) {
      LOG_F(ERROR, "EditorModule construction failed: surface registry is null!");
      throw std::invalid_argument(
        "EditorModule requires a non-null surface registry.");
    }
  }

  EditorModule::~EditorModule() {
    LOG_F(INFO, "EditorModule destroyed.");
  }

  auto EditorModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept
    -> bool {
    graphics_ = engine->GetGraphics();
    // Keep a non-owning reference to the engine so we can access other
    // engine modules (renderer) during command recording.
    engine_ = engine;
    return true;
  }

  auto EditorModule::OnFrameStart(engine::FrameContext& context) -> void {
    DCHECK_NOTNULL_F(registry_);

    ProcessSurfaceRegistrations();
    ProcessSurfaceDestructions();
    auto surfaces = ProcessResizeRequests();
    SyncSurfacesWithFrameContext(context, surfaces);

    // After scene creation/loading:
    if (scene_) {
      context.SetScene(observer_ptr{ scene_.get() });

      // Camera view creation is deferred to OnSceneMutation where the
      // camera's PerspectiveCamera and its viewport are configured.
      // This ensures the CameraView observes a correctly-initialized
      // camera/viewport pair, matching the example module behavior.
    }
  }

  void EditorModule::ProcessSurfaceRegistrations() {
    DCHECK_NOTNULL_F(registry_);

    auto pending = registry_->DrainPendingRegistrations();
    if (pending.empty()) {
      return;
    }

    for (auto& entry : pending) {
      const auto& key = entry.first;
      auto& surface = entry.second.first;
      auto& cb = entry.second.second;

      CHECK_NOTNULL_F(surface); // This is a bug if it happens.
      try {
        DLOG_F(INFO, "Processing pending surface registration for a surface (ptr={}).",
          fmt::ptr(surface.get()));
        // Register the surface in the registry's live entries
        registry_->CommitRegistration(key, surface);
      }
      catch (...) {
        // Registration failed
      }

      if (cb) {
        try {
          cb(true);
        }
        catch (...) {
          /* swallow */
        }
      }
    }
  }

  void EditorModule::ProcessSurfaceDestructions() {
    if (graphics_.expired()) {
      DLOG_F(WARNING, "Graphics instance is expired; cannot process deferred "
        "surface destructions.");
      return;
    }
    auto gfx = graphics_.lock();

    auto pending = registry_->DrainPendingDestructions();
    if (pending.empty()) {
      return;
    }

    for (auto& entry : pending) {
      const auto& key = entry.first;
      auto& surface = entry.second.first;
      auto& cb = entry.second.second;

      CHECK_NOTNULL_F(surface); // This is a bug if it happens.
      try {
        // Use the public Graphics API to register the surface for deferred
        // release. Do not use engine-internal APIs.
        gfx->RegisterDeferredRelease(std::move(surface));
      }
      catch (...) {
        // In case deferred handoff fails, still treat the destruction
        // as processed from the perspective of the editor.
      }

      if (cb) {
        try {
          cb(true);
        }
        catch (...) {
          /* swallow */
        }
      }
      // Remove any cached framebuffers for the surface so we don't hold
      // references to backbuffers after the surface is destroyed.
      surface_framebuffers_.erase(surface.get());
    }
  }

  auto EditorModule::ProcessResizeRequests()
    -> std::vector<std::shared_ptr<graphics::Surface>> {
    auto snapshot = registry_->SnapshotSurfaces();
    std::vector<std::shared_ptr<graphics::Surface>> surfaces;
    surfaces.reserve(snapshot.size());
    for (const auto& pair : snapshot) {
      const auto& key = pair.first;
      const auto& surface = pair.second;

      CHECK_NOTNULL_F(surface); // This is a bug if it happens.

      // If a resize was requested by the caller, apply it explicitly here
      // on the engine thread (frame start) and only then invoke any resize
      // callbacks with the outcome.
      if (surface->ShouldResize()) {
        DLOG_F(INFO, "Applying resize for a surface (ptr={}).", fmt::ptr(surface.get()));

        // Follow the safe-resize pattern used around the engine: ensure no
        // framebuffer or texture references hold on to swap-chain backbuffers
        // before invoking the native resize call. This avoids cases where
        // the backend reports an invalid-state when Resize() is called while
        // resources are still pinned.
        if (!graphics_.expired()) {
          auto gfx = graphics_.lock();
          // Make sure GPU work is finished before we attempt to drop
          // references which may still be in-flight.
          try {
            gfx->Flush();
          }
          catch (...) {
            DLOG_F(WARNING, "Graphics::Flush threw during pre-resize; continuing.");
          }

          // Drop any references that might keep the backbuffer alive.
          if (render_graph_ && render_graph_->GetRenderContext().framebuffer) {
            render_graph_->GetRenderContext().framebuffer.reset();
            DLOG_F(3, "Cleared cached framebuffer prior to surface resize.");
          }

          if (render_graph_) {
            auto& spc = render_graph_->GetShaderPassConfig();
            if (spc && spc->color_texture) {
              spc->color_texture.reset();
              DLOG_F(3, "Cleared shader pass color texture prior to surface resize.");
            }
            auto& tpc = render_graph_->GetTransparentPassConfig();
            if (tpc) {
              if (tpc->color_texture) {
                tpc->color_texture.reset();
                DLOG_F(3, "Cleared transparent pass color texture prior to surface resize.");
              }
              if (tpc->depth_texture) {
                tpc->depth_texture.reset();
                DLOG_F(3, "Cleared transparent pass depth texture prior to surface resize.");
              }
            }
          }

          // Clear any per-surface cached framebuffers. This drops our
          // shared references to swapchain backbuffers and any attached
          // depth textures so the backend can recreate them during Resize().
          auto it = surface_framebuffers_.find(surface.get());
          if (it != surface_framebuffers_.end()) {
            const auto cleared_count = it->second.size();
            it->second.clear();
            surface_framebuffers_.erase(it);
            DLOG_F(3, "Cleared {} cached framebuffer(s) for surface ptr={} prior to surface resize.", cleared_count, fmt::ptr(surface.get()));
          }

          // Flush again to ensure resource destruction isn't deferred and the
          // backend has no references to the old backbuffers before Resize().
          try {
            gfx->Flush();
          }
          catch (...) {
            DLOG_F(WARNING, "Graphics::Flush threw during pre-resize second-pass; continuing.");
          }
        }

        surface->Resize();

        // Drain and invoke callbacks after the explicit apply so they reflect
        // the actual post-resize state.
        auto resize_callbacks = registry_->DrainResizeCallbacks(key);
        auto back = surface->GetCurrentBackBuffer();
        bool ok = (back != nullptr);
        for (auto& rcb : resize_callbacks) {
          try {
            rcb(ok);
          }
          catch (...) {
            /* swallow */
          }
        }
      }

      surfaces.emplace_back(surface);
    }

    return surfaces; // This is the final snapshot of alive and ready surfaces.
  }

  auto EditorModule::OnSceneMutation(engine::FrameContext& context)
    -> co::Co<> {

    // TODO: delete this useless code - engine does scene update internally
    //const auto now = context.GetFrameStartTime();
    //double delta_seconds = 0.0;
    //if (last_frame_time_.time_since_epoch().count() == 0) {
    //    last_frame_time_ = now;
    //} else {
    //    delta_seconds = std::chrono::duration<double>(now - last_frame_time_).count();
    //}
    //last_frame_time_ = now;

    if (scene_) {
      // TODO: delete this useless code - engine does scene update internally
      //scene_->Update(static_cast<float>(delta_seconds));

      // TODO: Must have a different camera per surface being rendered to
      // This will allow us to show mulitple editor views (e.g. for multiple
      // viewports) if needed.

      // Create and use a dedicated editor camera node (never modify scene cameras)
      // Editor camera is created once and used for CameraView so scene cameras
      // remain under author control.
      auto scene_camera_node = this->FindNodeByName("MainCamera");
      if (scene_camera_node.IsAlive()) {
        auto sc_tf = scene_camera_node.GetTransform();
        const auto sc_pos = sc_tf.GetLocalPosition().value_or(glm::vec3(0.0F));
        DLOG_F(INFO, "Scene camera 'MainCamera' present at ({:.2f},{:.2f},{:.2f})", sc_pos.x, sc_pos.y, sc_pos.z);
      }

      auto editor_camera_node = this->FindNodeByName("EditorCamera");
      bool editor_camera_created = false;
      if (!editor_camera_node.IsAlive()) {
        editor_camera_node = scene_->CreateNode(std::string("EditorCamera"));
        LOG_F(INFO, "Created editor camera node 'EditorCamera' in scene");
        auto editor_cam = std::make_unique<oxygen::scene::PerspectiveCamera>(oxygen::scene::camera::ProjectionConvention::kD3D12);
        editor_camera_node.AttachCamera(std::move(editor_cam));
        editor_camera_created = true;
        editor_camera_node.GetTransform().SetLocalPosition(glm::vec3(1.5F, 2.0F, 14.0F));
        editor_camera_node.GetTransform().SetLocalRotation(glm::quat(glm::vec3(glm::radians(-20.0F), 0.0F, 0.0F)));
      }

      // Configure camera parameters and viewport for the editor camera
      {
        // FIXME: viewport settings and camera parameters should be per-surface
        // Use the first surface for viewport sizing if available
        float width = 1280.0f;
        float height = 720.0f;
        auto surfaces = context.GetSurfaces();
        if (!surfaces.empty() && surfaces.front()) {
          width = static_cast<float>(surfaces.front()->Width());
          height = static_cast<float>(surfaces.front()->Height());
        }

        auto cam_ref = editor_camera_node.GetCameraAs<oxygen::scene::PerspectiveCamera>();
        if (cam_ref) {
          const float aspect = height > 0.0f ? (width / height) : 1.0f;
          auto& cam = cam_ref->get();
          cam.SetFieldOfView(glm::radians(75.0F));
          cam.SetAspectRatio(aspect);
          cam.SetNearPlane(0.1F);
          cam.SetFarPlane(10000.0F);
          cam.SetViewport(oxygen::ViewPort{ .top_left_x = 0.0f,
                                            .top_left_y = 0.0f,
                                            .width = width,
                                            .height = height,
                                            .min_depth = 0.0f,
                                            .max_depth = 1.0f });
        }
      }

      // TODO: delete debug diags one editor module is stable
      // Minimal diagnostic: avoid dereferencing scene internals in logs to
      // prevent UB. Just report that we have a scene reference.
      {
        DLOG_F(2, "OnSceneMutation: scene present={}", static_cast<bool>(scene_));

        // Diagnostic logging: camera and root-node transforms to check for
        // potential frustum clipping / placement issues.
        if (editor_camera_node.IsAlive()) {
          auto cam_tf = editor_camera_node.GetTransform();
          const auto cam_pos = cam_tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 0.0F));
          DLOG_F(INFO, "OnSceneMutation: editor camera node '{}' local_pos=({:.2f},{:.2f},{:.2f})",
            editor_camera_node.GetName(), cam_pos.x, cam_pos.y, cam_pos.z);
        }

        try {
          for (auto& root : scene_->GetRootNodes()) {
            auto tf = root.GetTransform();
            const auto pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 0.0F));
            const auto scale = tf.GetLocalScale().value_or(glm::vec3(1.0F, 1.0F, 1.0F));
            DLOG_F(INFO, "OnSceneMutation: root node '{}' pos=({:.2f},{:.2f},{:.2f}) scale=({:.2f},{:.2f},{:.2f})",
              root.GetName(), pos.x, pos.y, pos.z, scale.x, scale.y, scale.z);
          }
        }
        catch (...) {
          DLOG_F(WARNING, "OnSceneMutation: failed to query root node transforms for diagnostics");
        }
      }

      // FIXME: per-surface camera views should be created if multiple surfaces
      // FIXME: for now engine clears views every frame. We rely on that behavior, but this may change in the future

      // Add camera view to the frame context. Match example behavior by
      // always creating a CameraView when a valid surface exists (the
      // camera and viewport were configured above).
      {
        auto surfaces = context.GetSurfaces();
        if (!surfaces.empty() && surfaces.front()) {
          auto surface = surfaces.front();
          // Use the dedicated editor camera for the CameraView so the
          // editor rendering is independent of any scene cameras.
          context.AddView(std::make_shared<oxygen::renderer::CameraView>(
            oxygen::renderer::CameraView::Params{
              .camera_node = editor_camera_node,
              .viewport = std::nullopt, // FIXME: per-surface viewport?
              .scissor = std::nullopt,  // FIXME: per-surface scissor?
              .pixel_jitter = glm::vec2(0.0F, 0.0F),
              .reverse_z = false,
              .mirrored = false,
            },
            surface
            ));
          LOG_F(INFO, "Editor Camera view created and set in frame context for a surface (ptr={}).", fmt::ptr(surface.get()));
        }
      }
    }
    co_return;
  }

  auto EditorModule::OnFrameGraph(engine::FrameContext& context) -> co::Co<> {
    // Ensure framebuffers are created like the examples. This will create a
    // depth texture per backbuffer and cached framebuffers for each surface.
    EnsureFramebuffers();

    // Ensure render passes are created/configured via the RenderGraph
    if (!render_graph_) {
      render_graph_ = std::make_unique<RenderGraph>();
    }
    render_graph_->SetupRenderPasses();

    co_return;
  }

  auto EditorModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<> {
    if (graphics_.expired()) {
      DLOG_F(WARNING, "Graphics instance is expired; cannot process deferred "
        "surface destructions.");
      co_return;
    }
    auto gfx = graphics_.lock();

    // We will only work on the surfaces registered in the frame context.
    auto surfaces = context.GetSurfaces();

    // Require engine access and renderer module. If they are missing, skip
    // rendering — the editor must own and provide a valid renderer context.
    if (!engine_) {
      DLOG_F(WARNING, "EditorModule::OnCommandRecord - no engine reference; skipping rendering");
      co_return;
    }

    auto renderer_opt = engine_->GetModule<oxygen::engine::Renderer>();
    if (!renderer_opt.has_value()) {
      DLOG_F(WARNING, "EditorModule::OnCommandRecord - renderer module not present; skipping rendering");
      co_return;
    }
    auto& renderer = renderer_opt->get();

    for (const auto& surface : surfaces) {
      // One command list (via command recorder acquisition) per surface.
      auto key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
      auto recorder = gfx->AcquireCommandRecorder(key, "EditorModule");
      if (!recorder) {
        continue;
      }

      auto back_buffer = surface->GetCurrentBackBuffer();
      if (!back_buffer) {
        continue;
      }

      // Use cached framebuffer vector for this surface. The
      // EnsureFramebuffers() call should have run earlier during FrameGraph
      // to populate `surface_framebuffers_`. If the cache is missing we
      // must not attempt to create transient framebuffers here — matching
      // the example/AppWindow behavior where cached framebuffers are required.
      const auto frame_count = static_cast<size_t>(oxygen::frame::kFramesInFlight.get());
      auto& fb_vec = surface_framebuffers_[surface.get()];
      if (fb_vec.empty()) {
        DLOG_F(WARNING, "EditorModule::OnCommandRecord - no cached framebuffers for surface; skipping rendering for surface ptr={}", fmt::ptr(surface.get()));
        continue;
      }

      std::shared_ptr<oxygen::graphics::Framebuffer> fb;
      const auto bb_index = surface->GetCurrentBackBufferIndex();
      if (bb_index < fb_vec.size()) {
        fb = fb_vec[bb_index];
      }
      if (!fb) {
        DLOG_F(WARNING, "EditorModule::OnCommandRecord - no framebuffer in cache for current backbuffer index; skipping surface ptr={}", fmt::ptr(surface.get()));
        continue;
      }

      // Diagnostic: report scene node counts to help determine whether
      // geometry exists in the scene. This is useful because passes ran
      // successfully but produced no visible geometry in the example runs.
      if (scene_) {
        size_t total_nodes = 0;
        std::function<void(oxygen::scene::SceneNode&)> count_nodes;
        count_nodes = [&](oxygen::scene::SceneNode& node) {
          if (!node.IsAlive()) return;
          ++total_nodes;
          auto child_opt = node.GetFirstChild();
          while (child_opt.has_value() && child_opt->IsAlive()) {
            count_nodes(*child_opt);
            child_opt = child_opt->GetNextSibling();
          }
          };
        for (auto& root : scene_->GetRootNodes()) {
          count_nodes(root);
        }
        DLOG_F(INFO, "EditorModule: scene node count = {}", total_nodes);
      }
      else {
        DLOG_F(INFO, "EditorModule: no scene present when recording commands");
      }

      // Update pass configs with the current framebuffer attachments via RenderGraph
      if (render_graph_) {
        auto& spc = render_graph_->GetShaderPassConfig();
        if (spc) {
          spc->color_texture = back_buffer;
        }
        auto& tpc = render_graph_->GetTransparentPassConfig();
        if (tpc) {
          tpc->color_texture = back_buffer;
          if (fb && fb->GetDescriptor().depth_attachment.IsValid()) {
            tpc->depth_texture = fb->GetDescriptor().depth_attachment.texture;
          }
          else {
            tpc->depth_texture.reset();
          }
        }
      }

      fb->PrepareForRender(*recorder);
      recorder->BindFrameBuffer(*fb);

      // Prepare the RenderGraph context for the current framebuffer so it
      // can be passed to Renderer::ExecuteRenderGraph. This avoids pulling
      // renderer internals into the editor.
      if (!render_graph_) {
        render_graph_ = std::make_unique<RenderGraph>();
      }
      render_graph_->PrepareForRenderFrame(fb);
      DLOG_F(INFO, "EditorModule: bound framebuffer in render context for surface ptr={} fb_ptr={}", fmt::ptr(surface.get()), fmt::ptr(fb.get()));
      // Run our passes inside the renderer execution wrapper to ensure
      // RenderContext.scene_constants (and related renderer-managed state)
      // are available to passes.
      co_await renderer.ExecuteRenderGraph(
        [&](const engine::RenderContext& ctx) -> co::Co<> {
          // Diagnostic logging to help trace why there are no draws. This
          // prints whether the renderer provided a prepared frame snapshot and
          // scene constants, the presence of a framebuffer, and view count.
          DLOG_F(INFO, "RenderGraph run: prepared_frame={}", static_cast<bool>(ctx.prepared_frame));
          // Keep diagnostics minimal and safe: only report presence of
          // prepared_frame. Avoid inspecting inner members which could
          // reference memory managed elsewhere and risk UB.
          DLOG_F(INFO, "RenderGraph run: scene_constants={}",
            static_cast<bool>(ctx.scene_constants));
          DLOG_F(INFO, "RenderGraph run: framebuffer={}", static_cast<bool>(ctx.framebuffer));
          try {
            // Avoid dereferencing or calling methods on potentially stale
            // RenderableView/Surface objects while logging (this can cause
            // UB if the underlying objects were destroyed). Log presence and
            // counts only.
            // Safely count views without dereferencing view objects to avoid
            // touching possibly-stale references that can cause crashes.
            size_t view_count = 0;
            // Avoid calling begin()/end() on different temporary transform_views
            // (which yields incompatible iterators); materialize the view range
            // into a local variable and iterate that.
            {
              auto views = context.GetViews();
              for (auto&& ignored_view : views) {
                ++view_count; // do NOT dereference or call methods on the view
              }
            }
            DLOG_F(INFO, "RenderGraph run: FrameContext views={}", view_count);
          }
          catch (...) {
            DLOG_F(INFO, "RenderGraph run: failed to query FrameContext views");
          }
          // Delegate pass execution to the shared RenderGraph component
          // which performs PrepareResources -> Execute for configured passes.
          if (!render_graph_) {
            render_graph_ = std::make_unique<RenderGraph>();
            render_graph_->SetupRenderPasses();
          }
          co_await render_graph_->RunPasses(ctx, *recorder);
          co_return;
        },
        render_graph_->GetRenderContext(), context);
    }

    co_return;
  }

  auto EditorModule::CreateScene(std::string_view name) -> void {
    LOG_F(INFO, "EditorModule::CreateScene called: name='{}'", name);
    scene_ = std::make_shared<oxygen::scene::Scene>(std::string(name));
    // NOTE: Do not auto-populate scenes with default geometry here. Mesh
    // creation should be driven by the managed layer (or other explicit
    // callers) when a node is created. This keeps native behavior minimal
    // and avoids surprising test/debug artefacts.
    // render_context_.scene = scene_; // Removed as RenderContext does not hold scene directly
  }

  auto EditorModule::EnsureFramebuffers() -> bool {
    if (graphics_.expired()) {
      DLOG_F(WARNING, "EnsureFramebuffers: no graphics instance available");
      return false;
    }

    auto gfx = graphics_.lock();
    auto snapshot = registry_->SnapshotSurfaces();
    bool any_created = false;

    const auto surface_iter = snapshot.begin();
    for (const auto& p : snapshot) {
      auto surface = p.second;
      if (!surface) {
        continue;
      }

      // If we already have cached framebuffers for this surface, skip.
      auto& fb_vec = surface_framebuffers_[surface.get()];
      if (!fb_vec.empty()) {
        continue;
      }

      const auto surface_width = surface->Width();
      const auto surface_height = surface->Height();
      const auto frame_count = static_cast<size_t>(oxygen::frame::kFramesInFlight.get());

      fb_vec.clear();
      fb_vec.resize(frame_count);

      for (size_t i = 0; i < frame_count; ++i) {
        auto cb = surface->GetBackBuffer(static_cast<uint32_t>(i));
        if (!cb) {
          continue;
        }

        // Create depth texture matching the example's flags/format. Prefer
        // using the backbuffer's descriptor width/height (these are the
        // actual texture dimensions). Fall back to the surface reported
        // size if the descriptor reports zero (some swapchain attach
        // timing can temporarily yield zero-sized descriptors).
        oxygen::graphics::TextureDesc depth_desc;
        const auto& cb_desc = cb->GetDescriptor();
        depth_desc.width = (cb_desc.width != 0) ? cb_desc.width : static_cast<uint32_t>(surface_width);
        depth_desc.height = (cb_desc.height != 0) ? cb_desc.height : static_cast<uint32_t>(surface_height);
        depth_desc.format = oxygen::Format::kDepth32;
        depth_desc.texture_type = oxygen::TextureType::kTexture2D;
        depth_desc.is_shader_resource = true;
        depth_desc.is_render_target = true;
        depth_desc.use_clear_value = true;
        depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
        depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

        DLOG_F(INFO, "EnsureFramebuffers: depth_desc width={} height={} (cb_desc width={} height={}) for surface ptr={}",
          depth_desc.width, depth_desc.height, cb_desc.width, cb_desc.height, fmt::ptr(surface.get()));

        std::shared_ptr<oxygen::graphics::Texture> depth_tex;
        try {
          depth_tex = gfx->CreateTexture(depth_desc);
        }
        catch (...) {
          DLOG_F(WARNING, "EnsureFramebuffers: CreateTexture for depth failed for surface ptr={}", fmt::ptr(surface.get()));
        }

        auto desc = oxygen::graphics::FramebufferDesc{}
        .AddColorAttachment(surface->GetBackBuffer(static_cast<uint32_t>(i)));
        if (depth_tex) {
          desc.SetDepthAttachment(depth_tex);
        }

        fb_vec[i] = gfx->CreateFramebuffer(desc);
        if (!fb_vec[i]) {
          DLOG_F(WARNING, "EnsureFramebuffers: failed to create framebuffer for surface ptr={} slot={}", fmt::ptr(surface.get()), i);
        }
        else {
          DLOG_F(INFO, "EditorModule: created cached framebuffer for surface ptr={} slot={} fb_ptr={} color_ptr={}", fmt::ptr(surface.get()), i, fmt::ptr(fb_vec[i].get()), fmt::ptr(cb.get()));
          any_created = true;
        }
      }
    }

    return any_created;
  }

  auto EditorModule::RemoveSceneNode(std::string_view name) -> void {
    if (!scene_) {
      DLOG_F(WARNING, "Cannot remove scene node: no scene exists");
      return;
    }

    auto node = FindNodeByName(name);
    if (node.IsAlive()) {
      scene_->DestroyNode(node);
    }
    else {
      DLOG_F(WARNING, "Cannot remove scene node: node '{}' not found", name);
    }
  }

  // EditorModule no longer owns pass objects; RenderGraph owns and manages them.

  auto EditorModule::FindNodeByName(std::string_view name) -> oxygen::scene::SceneNode {
    if (!scene_) {
      return oxygen::scene::SceneNode();
    }

    // Search through all root nodes and their children
    auto root_nodes = scene_->GetRootNodes();
    for (auto& root : root_nodes) {
      if (root.GetName() == name) {
        return root;
      }

      // Recursively search children
      std::function<oxygen::scene::SceneNode(oxygen::scene::SceneNode&)> search_children;
      search_children = [&](oxygen::scene::SceneNode& parent) -> oxygen::scene::SceneNode {
        auto child_opt = parent.GetFirstChild();
        while (child_opt.has_value() && child_opt->IsAlive()) {
          if (child_opt->GetName() == name) {
            return *child_opt;
          }

          auto result = search_children(*child_opt);
          if (result.IsAlive()) {
            return result;
          }

          child_opt = child_opt->GetNextSibling();
        }
        return oxygen::scene::SceneNode();
        };

      auto result = search_children(root);
      if (result.IsAlive()) {
        return result;
      }
    }

    return oxygen::scene::SceneNode();
  }

  auto EditorModule::CreateSceneNode(std::string_view name, std::string_view parent_name) -> void {
    LOG_F(INFO, "EditorModule::CreateSceneNode called: name='{}' parent='{}'", name, parent_name);
    if (!scene_) {
      DLOG_F(WARNING, "Cannot create scene node: no scene exists");
      return;
    }

    if (parent_name.empty()) {
      // Create as root node
      [[maybe_unused]] auto node = scene_->CreateNode(std::string(name));
    }
    else {
      // Find parent and create as child
      auto parent = FindNodeByName(parent_name);
      if (parent.IsAlive()) {
        [[maybe_unused]] auto child_node = scene_->CreateChildNode(parent, std::string(name));
        // Creation of geometry (meshes) is intentionally left to managed
        // callers or other explicit APIs. Do not auto-create meshes here.
      }
      else {
        DLOG_F(WARNING, "Cannot create scene node '{}': parent '{}' not found",
          name, parent_name);
      }
    }
  }

  auto EditorModule::SetLocalTransform(std::string_view node_name,
    const glm::vec3& position,
    const glm::quat& rotation,
    const glm::vec3& scale) -> void {
    if (!scene_) {
      DLOG_F(WARNING, "Cannot set transform: no scene exists");
      return;
    }

    auto node = FindNodeByName(node_name);
    if (!node.IsAlive()) {
      DLOG_F(WARNING, "Cannot set transform: node '{}' not found", node_name);
      return;
    }

    auto transform = node.GetTransform();
    [[maybe_unused]] auto pos_result = transform.SetLocalPosition(position);
    [[maybe_unused]] auto rot_result = transform.SetLocalRotation(rotation);
    [[maybe_unused]] auto scale_result = transform.SetLocalScale(scale);
  }

  auto EditorModule::CreateBasicMesh(std::string_view node_name, std::string_view mesh_type) -> void {
    LOG_F(INFO, "CreateBasicMesh called: node='{}' mesh_type='{}'", node_name, mesh_type);
    if (!scene_) {
      DLOG_F(WARNING, "Cannot create mesh: no scene exists");
      return;
    }

    auto node = FindNodeByName(node_name);
    if (!node.IsAlive()) {
      DLOG_F(WARNING, "Cannot create mesh: node '{}' not found", node_name);
      return;
    }

    // Generate mesh geometry based on type
    std::optional<std::pair<std::vector<oxygen::data::Vertex>, std::vector<uint32_t>>> mesh_data;

    // Normalize mesh_type to lower-case to accept various casings from callers
    std::string mesh_type_str(mesh_type);
    std::transform(mesh_type_str.begin(), mesh_type_str.end(), mesh_type_str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (mesh_type_str == "cube") {
      mesh_data = oxygen::data::MakeCubeMeshAsset();
    }
    else if (mesh_type_str == "sphere") {
      mesh_data = oxygen::data::MakeSphereMeshAsset();
    }
    else if (mesh_type_str == "plane") {
      mesh_data = oxygen::data::MakePlaneMeshAsset();
    }
    else if (mesh_type_str == "cylinder") {
      mesh_data = oxygen::data::MakeCylinderMeshAsset();
    }
    else if (mesh_type_str == "cone") {
      mesh_data = oxygen::data::MakeConeMeshAsset();
    }
    else if (mesh_type_str == "torus") {
      mesh_data = oxygen::data::MakeTorusMeshAsset();
    }
    else {
      DLOG_F(WARNING, "Unknown mesh type: {}", mesh_type);
      return;
    }

    if (!mesh_data.has_value()) {
      DLOG_F(WARNING, "Failed to generate {} mesh", mesh_type);
      return;
    }

    // Create a default material
    oxygen::data::pak::MaterialAssetDesc material_desc{};
    material_desc.header.asset_type = 7; // Material type
    const auto name_str = std::string("DefaultMaterial_") + std::string(mesh_type);
    const std::size_t maxn = sizeof(material_desc.header.name) - 1;
    const std::size_t n = (std::min)(maxn, name_str.size());
    std::memcpy(material_desc.header.name, name_str.c_str(), n);
    material_desc.header.name[n] = '\0';

    material_desc.material_domain = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);

    // Choose a deterministic color for this default material based on the
    // node name so different meshes are visually distinct when no explicit
    // material is provided. This aids testing and scene debugging.
    const std::vector<std::array<float, 4>> palette = {
      { 0.82f, 0.24f, 0.21f, 1.0f }, // red
      { 0.20f, 0.63f, 0.17f, 1.0f }, // green
      { 0.18f, 0.49f, 0.74f, 1.0f }, // blue
      { 0.95f, 0.77f, 0.06f, 1.0f }, // yellow
      { 0.72f, 0.27f, 0.82f, 1.0f }, // magenta
      { 0.06f, 0.74f, 0.70f, 1.0f }, // cyan
      { 0.88f, 0.56f, 0.31f, 1.0f }, // orange
    };

    // Hash the node name to pick a palette entry. Use node_name when
    // available so different scene nodes map to different colors.
    std::string key = std::string(node_name);
    const auto h = std::hash<std::string>{}(key);
    const auto idx = static_cast<size_t>(h % palette.size());
    material_desc.base_color[0] = palette[idx][0];
    material_desc.base_color[1] = palette[idx][1];
    material_desc.base_color[2] = palette[idx][2];
    material_desc.base_color[3] = palette[idx][3];

    auto material = std::make_shared<const oxygen::data::MaterialAsset>(
      material_desc, std::vector<oxygen::data::ShaderReference>{});

    // Build mesh using MeshBuilder
    auto& [vertices, indices] = mesh_data.value();
    auto mesh_builder = oxygen::data::MeshBuilder(0, std::string(mesh_type))
      .WithVertices(vertices)
      .WithIndices(indices);

    // Create submesh with material
    oxygen::data::pak::MeshViewDesc view_desc{};
    view_desc.first_vertex = 0;
    view_desc.vertex_count = static_cast<uint32_t>(vertices.size());
    view_desc.first_index = 0;
    view_desc.index_count = static_cast<uint32_t>(indices.size());

    auto mesh = mesh_builder
      .BeginSubMesh("default", material)
      .WithMeshView(view_desc)
      .EndSubMesh()
      .Build();

    // Create GeometryAsset
    oxygen::data::pak::GeometryAssetDesc geo_desc{};
    geo_desc.header.asset_type = 6; // Geometry type
    const std::size_t geo_n = (std::min)(maxn, mesh_type.size());
    std::memcpy(geo_desc.header.name, mesh_type.data(), geo_n);
    geo_desc.header.name[geo_n] = '\0';

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lod_meshes;
    lod_meshes.push_back(std::move(mesh));

    auto geometry = std::make_shared<oxygen::data::GeometryAsset>(geo_desc, std::move(lod_meshes));

    // Attach to node
    auto renderable = node.GetRenderable();
    renderable.SetGeometry(geometry);
    LOG_F(INFO, "CreateBasicMesh: attached geometry '{}' to node '{}', vertices={} indices={}", mesh_type, node_name, vertices.size(), indices.size());
  }

  auto EditorModule::SyncSurfacesWithFrameContext(
    engine::FrameContext& context,
    const std::vector<std::shared_ptr<graphics::Surface>>& surfaces) -> void {
    std::unordered_set<const graphics::Surface*> desired;
    desired.reserve(surfaces.size());
    for (const auto& surface : surfaces) {
      DCHECK_NOTNULL_F(surface);
      desired.insert(surface.get());
    }

    // Attention: do not mutate context surfaces while iterating over.

    std::vector<size_t> removal_indices;
    removal_indices.reserve(surface_indices_.size());
    for (const auto& [raw, index] : surface_indices_) {
      if (!desired.contains(raw)) {
        removal_indices.push_back(index);
      }
    }

    std::sort(removal_indices.rbegin(), removal_indices.rend());
    for (auto index : removal_indices) {
      context.RemoveSurfaceAt(index);
    }

    auto context_surfaces = context.GetSurfaces();
    std::unordered_map<const graphics::Surface*, size_t> current_indices;
    current_indices.reserve(context_surfaces.size());
    for (size_t i = 0; i < context_surfaces.size(); ++i) {
      const auto& entry = context_surfaces[i];
      if (entry) {
        current_indices.emplace(entry.get(), i);
      }
    }

    for (const auto& surface : surfaces) {
      const auto* raw = surface.get();
      if (current_indices.contains(raw)) {
        continue;
      }

      context.AddSurface(surface);
      context_surfaces = context.GetSurfaces();
      current_indices[raw] = context_surfaces.size() - 1;
    }

    surface_indices_.clear();
    for (const auto& surface : surfaces) {
      const auto* raw = surface.get();
      auto iter = current_indices.find(raw);
      if (iter == current_indices.end()) {
        continue;
      }

      const auto index = iter->second;
      surface_indices_.emplace(raw, index);
      context.SetSurfacePresentable(index, true);
    }
  }

} // namespace Oxygen::Editor::EngineInterface
