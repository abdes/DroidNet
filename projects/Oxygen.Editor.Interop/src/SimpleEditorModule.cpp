#include "SimpleEditorModule.h"
#pragma unmanaged

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Oxygen::Editor::EngineInterface {

  using namespace oxygen;

  SimpleEditorModule::SimpleEditorModule(std::shared_ptr<SurfaceRegistry> registry)
    : registry_(std::move(registry))
  {
  }

  SimpleEditorModule::~SimpleEditorModule()
  {
    LOG_F(INFO, "SimpleEditorModule destroyed");
  }

  auto SimpleEditorModule::GetSupportedPhases() const noexcept -> engine::ModulePhaseMask
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart, core::PhaseId::kCommandRecord>();
  }

  auto SimpleEditorModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
  {
    graphics_ = engine->GetGraphics();
    return true;
  }

  auto SimpleEditorModule::OnFrameStart(engine::FrameContext& context) -> void
  {
    (void)EnsureSurfacesRegistered(context);
  }

  auto SimpleEditorModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<>
  {
    auto surfaces = EnsureSurfacesRegistered(context);

    auto gfx = graphics_.lock();
    if (!gfx || surfaces.empty()) {
      co_return;
    }

    for (const auto& surface : surfaces) {
      if (!surface) {
        continue;
      }

      auto key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
      auto recorder = gfx->AcquireCommandRecorder(key, "SimpleEditorModule");
      if (!recorder) {
        continue;
      }

      auto back_buffer = surface->GetCurrentBackBuffer();
      if (!back_buffer) {
        continue;
      }

      graphics::FramebufferDesc fb_desc;
      fb_desc.color_attachments.push_back(graphics::FramebufferAttachment{
          .texture = back_buffer,
          .format = back_buffer->GetDescriptor().format
        });

      auto fb = gfx->CreateFramebuffer(fb_desc);
      if (!fb) {
        continue;
      }

      fb->PrepareForRender(*recorder);
      recorder->BindFrameBuffer(*fb);
      recorder->ClearFramebuffer(*fb,
        std::vector<std::optional<graphics::Color>>{
        graphics::Color(0.392f, 0.584f, 0.929f, 1.0f),
      });
    }

    co_return;
  }

  auto SimpleEditorModule::EnsureSurfacesRegistered(engine::FrameContext& context)
    -> std::vector<std::shared_ptr<graphics::Surface>>
  {
    if (!registry_) {
      std::vector<std::shared_ptr<graphics::Surface>> empty;
      RefreshSurfaceIndices(context, empty);
      surface_indices_.clear();
      return empty;
    }

    auto snapshot = registry_->SnapshotSurfaces();
    RefreshSurfaceIndices(context, snapshot);
    return snapshot;
  }

  auto SimpleEditorModule::RefreshSurfaceIndices(
    engine::FrameContext& context,
    const std::vector<std::shared_ptr<graphics::Surface>>& snapshot) -> void
  {
    std::unordered_set<const graphics::Surface*> desired;
    desired.reserve(snapshot.size());
    for (const auto& surface : snapshot) {
      if (surface) {
        desired.insert(surface.get());
      }
    }

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

    for (const auto& surface : snapshot) {
      if (!surface) {
        continue;
      }

      const auto* raw = surface.get();
      if (current_indices.contains(raw)) {
        continue;
      }

      context.AddSurface(surface);
      context_surfaces = context.GetSurfaces();
      current_indices[raw] = context_surfaces.size() - 1;
    }

    surface_indices_.clear();
    for (const auto& surface : snapshot) {
      if (!surface) {
        continue;
      }

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
