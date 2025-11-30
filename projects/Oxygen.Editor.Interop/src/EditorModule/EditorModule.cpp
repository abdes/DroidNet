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
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/CameraView.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>

#include "EditorModule/EditorModule.h"
#include "EditorModule/NodeRegistry.h"
#include "EditorModule/RenderGraph.h"

namespace oxygen::interop::module {

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

  EditorModule::~EditorModule() { LOG_F(INFO, "EditorModule destroyed."); }

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
        DLOG_F(INFO,
          "Processing pending surface registration for a surface (ptr={}).",
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
      // Also cleanup the camera associated with this surface.
      CleanupSurfaceCamera(surface.get());
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
        DLOG_F(INFO, "Applying resize for a surface (ptr={}).",
          fmt::ptr(surface.get()));

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
            DLOG_F(WARNING,
              "Graphics::Flush threw during pre-resize; continuing.");
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
              DLOG_F(
                3,
                "Cleared shader pass color texture prior to surface resize.");
            }
            auto& tpc = render_graph_->GetTransparentPassConfig();
            if (tpc) {
              if (tpc->color_texture) {
                tpc->color_texture.reset();
                DLOG_F(3, "Cleared transparent pass color texture prior to "
                  "surface resize.");
              }
              if (tpc->depth_texture) {
                tpc->depth_texture.reset();
                DLOG_F(3, "Cleared transparent pass depth texture prior to "
                  "surface resize.");
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
            DLOG_F(3,
              "Cleared {} cached framebuffer(s) for surface ptr={} prior to "
              "surface resize.",
              cleared_count, fmt::ptr(surface.get()));
          }

          // Flush again to ensure resource destruction isn't deferred and the
          // backend has no references to the old backbuffers before Resize().
          try {
            gfx->Flush();
          }
          catch (...) {
            DLOG_F(WARNING, "Graphics::Flush threw during pre-resize "
              "second-pass; continuing.");
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

        // Update editor camera aspect ratio after resize
        if (ok && back) {
          const auto& bb_desc = back->GetDescriptor();
          const float surface_width = static_cast<float>(surface->Width());
          const float surface_height = static_cast<float>(surface->Height());
          const float bb_width = static_cast<float>(bb_desc.width);
          const float bb_height = static_cast<float>(bb_desc.height);

          LOG_F(INFO,
            "ProcessResizeRequests: surface reports {}x{}, backbuffer is {}x{}",
            surface_width, surface_height, bb_width, bb_height);

          // Use surface pointer to identify which camera to update
          EnsureEditorCamera(surface.get(), bb_width, bb_height);
        }
      }

      surfaces.emplace_back(surface);
    }

    return surfaces; // This is the final snapshot of alive and ready surfaces.
  }

  auto EditorModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<> {

    // Drain the command queue and execute all pending commands
    CommandContext cmd_context{ scene_.get() };
    command_queue_.Drain([&](std::unique_ptr<EditorCommand>& cmd) {
      if (cmd) {
        cmd->Execute(cmd_context);
      }
      });

    if (scene_) {
      // Iterate over ALL surfaces and create camera views for each with
      // correct aspect ratios. This fixes the multi-panel layout issue where
      // surfaces with different aspect ratios were all using the main panel's
      // aspect ratio.
      auto surfaces = context.GetSurfaces();

      for (const auto& surface : surfaces) {
        if (!surface) {
          continue;
        }

        // Get surface dimensions
        float width = 1280.0f;
        float height = 720.0f;
        auto back = surface->GetCurrentBackBuffer();
        if (back) {
          const auto& desc = back->GetDescriptor();
          width = static_cast<float>(desc.width);
          height = static_cast<float>(desc.height);
          DLOG_F(INFO, "OnSceneMutation: surface ptr={} dimensions {}x{}",
                 fmt::ptr(surface.get()), width, height);
        } else {
          width = static_cast<float>(surface->Width());
          height = static_cast<float>(surface->Height());
          DLOG_F(INFO, "OnSceneMutation: surface ptr={} using surface dimensions {}x{}",
                 fmt::ptr(surface.get()), width, height);
        }

        // Ensure camera exists for this surface with correct aspect ratio
        EnsureEditorCamera(surface.get(), width, height);

        // Create camera view for this surface
        auto it = surface_cameras_.find(surface.get());
        if (it != surface_cameras_.end() && it->second.IsAlive()) {
          context.AddView(std::make_shared<oxygen::renderer::CameraView>(
            oxygen::renderer::CameraView::Params{
              .camera_node = it->second,
              .viewport = std::nullopt,
              .scissor = std::nullopt,
              .pixel_jitter = glm::vec2(0.0F, 0.0F),
              .reverse_z = false,
              .mirrored = false,
            },
            surface));
          LOG_F(INFO,
            "Editor Camera view created for surface (ptr={}) with dedicated camera.",
            fmt::ptr(surface.get()));
        }
      }

      // TODO: delete debug diags once editor module is stable
      // Minimal diagnostic: avoid dereferencing scene internals in logs to
      // prevent UB. Just report that we have a scene reference.
      {
        DLOG_F(2, "OnSceneMutation: scene present={}", static_cast<bool>(scene_));

        // Diagnostic logging: camera and root-node transforms to check for
        // potential frustum clipping / placement issues.
        // Log all surface cameras
        for (const auto& cam_entry : surface_cameras_) {
          if (cam_entry.second.IsAlive()) {
            auto cam_tf = cam_entry.second.GetTransform();
            const auto cam_pos =
              cam_tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 0.0F));
            DLOG_F(INFO,
              "OnSceneMutation: editor camera node '{}' for surface ptr={} "
              "local_pos=({:.2f},{:.2f},{:.2f})",
              cam_entry.second.GetName(), fmt::ptr(cam_entry.first),
              cam_pos.x, cam_pos.y, cam_pos.z);
          }
        }

        try {
          for (auto& root : scene_->GetRootNodes()) {
            auto tf = root.GetTransform();
            const auto pos =
              tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 0.0F));
            const auto scale =
              tf.GetLocalScale().value_or(glm::vec3(1.0F, 1.0F, 1.0F));
            DLOG_F(INFO,
              "OnSceneMutation: root node '{}' pos=({:.2f},{:.2f},{:.2f}) "
              "scale=({:.2f},{:.2f},{:.2f})",
              root.GetName(), pos.x, pos.y, pos.z, scale.x, scale.y,
              scale.z);
          }
        }
        catch (...) {
          DLOG_F(WARNING, "OnSceneMutation: failed to query root node transforms "
            "for diagnostics");
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
      DLOG_F(WARNING, "EditorModule::OnCommandRecord - no engine reference; "
        "skipping rendering");
      co_return;
    }

    auto renderer_opt = engine_->GetModule<oxygen::engine::Renderer>();
    if (!renderer_opt.has_value()) {
      DLOG_F(WARNING, "EditorModule::OnCommandRecord - renderer module not "
        "present; skipping rendering");
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
      const auto frame_count =
        static_cast<size_t>(oxygen::frame::kFramesInFlight.get());
      auto& fb_vec = surface_framebuffers_[surface.get()];
      if (fb_vec.empty()) {
        DLOG_F(WARNING,
          "EditorModule::OnCommandRecord - no cached framebuffers for "
          "surface; skipping rendering for surface ptr={}",
          fmt::ptr(surface.get()));
        continue;
      }

      std::shared_ptr<oxygen::graphics::Framebuffer> fb;
      const auto bb_index = surface->GetCurrentBackBufferIndex();
      if (bb_index < fb_vec.size()) {
        fb = fb_vec[bb_index];
      }
      if (!fb) {
        DLOG_F(WARNING,
          "EditorModule::OnCommandRecord - no framebuffer in cache for "
          "current backbuffer index; skipping surface ptr={}",
          fmt::ptr(surface.get()));
        continue;
      }

      // Diagnostic: report scene node counts to help determine whether
      // geometry exists in the scene. This is useful because passes ran
      // successfully but produced no visible geometry in the example runs.
      if (scene_) {
        size_t total_nodes = 0;
        std::function<void(oxygen::scene::SceneNode&)> count_nodes;
        count_nodes = [&](oxygen::scene::SceneNode& node) {
          if (!node.IsAlive())
            return;
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

      // Update pass configs with the current framebuffer attachments via
      // RenderGraph
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
      DLOG_F(INFO,
        "EditorModule: bound framebuffer in render context for surface "
        "ptr={} fb_ptr={}",
        fmt::ptr(surface.get()), fmt::ptr(fb.get()));
      // Run our passes inside the renderer execution wrapper to ensure
      // RenderContext.scene_constants (and related renderer-managed state)
      // are available to passes.
      co_await renderer.ExecuteRenderGraph(
        [&](const engine::RenderContext& ctx) -> co::Co<> {
          // Diagnostic logging to help trace why there are no draws. This
          // prints whether the renderer provided a prepared frame snapshot and
          // scene constants, the presence of a framebuffer, and view count.
          DLOG_F(INFO, "RenderGraph run: prepared_frame={}",
            static_cast<bool>(ctx.prepared_frame));
          // Keep diagnostics minimal and safe: only report presence of
          // prepared_frame. Avoid inspecting inner members which could
          // reference memory managed elsewhere and risk UB.
          DLOG_F(INFO, "RenderGraph run: scene_constants={}",
            static_cast<bool>(ctx.scene_constants));
          DLOG_F(INFO, "RenderGraph run: framebuffer={}",
            static_cast<bool>(ctx.framebuffer));
          try {
            // Avoid dereferencing or calling methods on potentially stale
            // RenderableView/Surface objects while logging (this can cause
            // UB if the underlying objects were destroyed). Log presence and
            // counts only.
            // Safely count views without dereferencing view objects to avoid
            // touching possibly-stale references that can cause crashes.
            size_t view_count = 0;
            // Avoid calling begin()/end() on different temporary
            // transform_views (which yields incompatible iterators);
            // materialize the view range into a local variable and iterate
            // that.
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
    // render_context_.scene = scene_; // Removed as RenderContext does not hold
    // scene directly
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
      const auto& surface = p.second;
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
      const auto frame_count =
        static_cast<size_t>(oxygen::frame::kFramesInFlight.get());

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
        depth_desc.width = (cb_desc.width != 0)
          ? cb_desc.width
          : static_cast<uint32_t>(surface_width);
        depth_desc.height = (cb_desc.height != 0)
          ? cb_desc.height
          : static_cast<uint32_t>(surface_height);
        depth_desc.format = oxygen::Format::kDepth32;
        depth_desc.texture_type = oxygen::TextureType::kTexture2D;
        depth_desc.is_shader_resource = true;
        depth_desc.is_render_target = true;
        depth_desc.use_clear_value = true;
        depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
        depth_desc.initial_state = oxygen::graphics::ResourceStates::kDepthWrite;

        DLOG_F(INFO,
          "EnsureFramebuffers: depth_desc width={} height={} (cb_desc "
          "width={} height={}) for surface ptr={}",
          depth_desc.width, depth_desc.height, cb_desc.width, cb_desc.height,
          fmt::ptr(surface.get()));

        std::shared_ptr<oxygen::graphics::Texture> depth_tex;
        try {
          depth_tex = gfx->CreateTexture(depth_desc);
        }
        catch (...) {
          DLOG_F(WARNING,
            "EnsureFramebuffers: CreateTexture for depth failed for surface "
            "ptr={}",
            fmt::ptr(surface.get()));
        }

        auto desc = oxygen::graphics::FramebufferDesc{}.AddColorAttachment(
          surface->GetBackBuffer(static_cast<uint32_t>(i)));
        if (depth_tex) {
          desc.SetDepthAttachment(depth_tex);
        }

        fb_vec[i] = gfx->CreateFramebuffer(desc);
        if (!fb_vec[i]) {
          DLOG_F(WARNING,
            "EnsureFramebuffers: failed to create framebuffer for surface "
            "ptr={} slot={}",
            fmt::ptr(surface.get()), i);
        }
        else {
          DLOG_F(INFO,
            "EditorModule: created cached framebuffer for surface ptr={} "
            "slot={} fb_ptr={} color_ptr={}",
            fmt::ptr(surface.get()), i, fmt::ptr(fb_vec[i].get()),
            fmt::ptr(cb.get()));
          any_created = true;
        }
      }
    }

    return any_created;
  }


  void EditorModule::EnsureEditorCamera(const oxygen::graphics::Surface* surface,
                                       float width,
                                       float height) {
    if (!scene_ || !surface) {
      return;
    }

    // Check if camera exists for this surface
    auto it = surface_cameras_.find(surface);
    bool camera_exists = (it != surface_cameras_.end() && it->second.IsAlive());

    // Create camera if it doesn't exist
    if (!camera_exists) {
      std::string camera_name = fmt::format("EditorCamera_{}",
                                           fmt::ptr(surface));
      auto camera_node = scene_->CreateNode(camera_name);
      auto editor_cam = std::make_unique<oxygen::scene::PerspectiveCamera>(
        oxygen::scene::camera::ProjectionConvention::kD3D12);
      camera_node.AttachCamera(std::move(editor_cam));

      // DIAGNOSTIC: Vary camera distance per surface to confirm each surface
      // gets its own camera. Each camera will be at a different distance:
      // 15, 20, 25, 30, etc. based on how many cameras already exist.
      const float base_distance = 15.0F;
      const float distance_offset = static_cast<float>(surface_cameras_.size()) * 5.0F;
      const float distance = base_distance + distance_offset;

      // Set initial camera position
      glm::vec3 position(0.0F, 0.0F, distance);
      glm::vec3 target(0.0F, 0.0F, 0.0F);
      glm::vec3 up(0.0F, 1.0F, 0.0F);
      camera_node.GetTransform().SetLocalPosition(position);
      glm::vec3 direction = glm::normalize(target - position);
      glm::quat orientation = glm::quatLookAt(direction, up);
      camera_node.GetTransform().SetLocalRotation(orientation);

      // Store the camera
      surface_cameras_[surface] = std::move(camera_node);
      LOG_F(INFO, "Created editor camera '{}' for surface ptr={} at distance={:.1f}",
            camera_name, fmt::ptr(surface), distance);

      it = surface_cameras_.find(surface);
    }

    // ALWAYS update camera aspect ratio and viewport, even if camera already exists.
    // This is critical because surfaces are recycled by SurfaceRegistry - the same
    // surface pointer can be reused for different viewports with different dimensions.
    // Recalculating aspect ratio is cheap and ensures correctness.
    if (it != surface_cameras_.end() && it->second.IsAlive()) {
      auto cam_ref = it->second.GetCameraAs<oxygen::scene::PerspectiveCamera>();
      if (cam_ref) {
        const float aspect = (width > 0.0f && height > 0.0f) ? (width / height) : 1.0f;
        auto& cam = cam_ref->get();
        cam.SetFieldOfView(glm::radians(60.0F));
        cam.SetAspectRatio(aspect);
        cam.SetNearPlane(0.1F);
        cam.SetFarPlane(10000.0F);
        cam.SetViewport(oxygen::ViewPort{
          .top_left_x = 0.0f,
          .top_left_y = 0.0f,
          .width = width,
          .height = height,
          .min_depth = 0.0f,
          .max_depth = 1.0f
        });
        DLOG_F(2,
          "Camera for surface ptr={} configured: {}x{} aspect={:.3f}",
          fmt::ptr(surface), width, height, aspect);
      }
    }
  }

  void EditorModule::CleanupSurfaceCamera(const oxygen::graphics::Surface* surface) {
    if (!surface) {
      return;
    }

    auto it = surface_cameras_.find(surface);
    if (it != surface_cameras_.end()) {
      if (it->second.IsAlive()) {
        // Detach camera component before destroying node
        it->second.DetachCamera();
        // Node will be cleaned up by scene when map entry is removed
        LOG_F(INFO, "Cleaned up camera for surface ptr={}", fmt::ptr(surface));
      }
      surface_cameras_.erase(it);
    }
  }

  void EditorModule::Enqueue(std::unique_ptr<EditorCommand> cmd) {
    command_queue_.Enqueue(std::move(cmd));
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

} // namespace oxygen::interop::module
