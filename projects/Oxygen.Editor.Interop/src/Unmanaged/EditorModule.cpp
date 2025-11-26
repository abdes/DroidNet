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

#include "Unmanaged/EditorModule.h"

namespace Oxygen::Editor::EngineInterface {

  using namespace oxygen;

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
    return true;
  }

  auto EditorModule::OnFrameStart(engine::FrameContext& context) -> void {
    DCHECK_NOTNULL_F(registry_);

    ProcessSurfaceRegistrations();
    ProcessSurfaceDestructions();
    auto surfaces = ProcessResizeRequests();
    SyncSurfacesWithFrameContext(context, surfaces);
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
        DLOG_F(INFO, "Processing pending surface registration for: '{}'.",
          surface->GetName());
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
        DLOG_F(INFO, "Processing deferred surface destruction for: '{}'.",
          surface->GetName());
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
        DLOG_F(INFO, "Applying resize for surface '{}'.", surface->GetName());
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

  auto EditorModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<> {
    if (graphics_.expired()) {
      DLOG_F(WARNING, "Graphics instance is expired; cannot process deferred "
        "surface destructions.");
      co_return;
    }
    auto gfx = graphics_.lock();

    auto snapshot_pairs = registry_->SnapshotSurfaces();
    if (snapshot_pairs.empty()) {
      // No surfaces -> no work to be done.
      co_return;
    }

    std::vector<std::shared_ptr<graphics::Surface>> surfaces;
    surfaces.reserve(snapshot_pairs.size());
    for (const auto& p : snapshot_pairs) {
      DCHECK_NOTNULL_F(p.second);
      surfaces.push_back(p.second);
    }

    SyncSurfacesWithFrameContext(context, surfaces);

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

      // FIXME: framebuffers should be cached/reused and follow surface lifecycle,
      // not created every frame.
      graphics::FramebufferDesc fb_desc;
      fb_desc.color_attachments.push_back(graphics::FramebufferAttachment{
          .texture = back_buffer, .format = back_buffer->GetDescriptor().format });

      auto fb = gfx->CreateFramebuffer(fb_desc);
      if (!fb) {
        continue;
      }

      fb->PrepareForRender(*recorder);
      recorder->BindFrameBuffer(*fb);

      // Derive a stable background color from the surface name so the same
      // surface always gets the same color. We hash the name and extract three
      // bytes to form RGB components then bias them into a pleasant visible
      // range.
      auto make_color_from_name = [](std::string_view name) -> graphics::Color {
        // Hash the name (convert to std::string to use the std::hash
        // specialization that is guaranteed for std::string across standards).
        const size_t h = std::hash<std::string>{}(std::string(name));

        uint8_t rb = static_cast<uint8_t>((h >> 0) & 0xFF);
        uint8_t gb = static_cast<uint8_t>((h >> 8) & 0xFF);
        uint8_t bb = static_cast<uint8_t>((h >> 16) & 0xFF);

        auto to_comp = [](uint8_t v) -> float {
          // Map [0,255] -> [0.30, 0.90] to avoid very dark or overly bright
          // colors.
          return 0.30f + (static_cast<float>(v) / 255.0f) * 0.60f;
          };

        return graphics::Color(to_comp(rb), to_comp(gb), to_comp(bb), 1.0f);
        };

      graphics::Color bg_color = make_color_from_name(surface->GetName());
      recorder->ClearFramebuffer(
        *fb, std::vector<std::optional<graphics::Color>>{bg_color});
    }

    co_return;
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
