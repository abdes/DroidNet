#include "SimpleEditorModule.h"
#pragma unmanaged

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <optional>
#include <vector>

namespace Oxygen::Editor::EngineInterface {

using namespace oxygen;

SimpleEditorModule::SimpleEditorModule(std::shared_ptr<graphics::Surface> surface)
    : surface_(std::move(surface))
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
    EnsureSurfaceRegistered(context);
}

auto SimpleEditorModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<>
{
    EnsureSurfaceRegistered(context);

    auto gfx = graphics_.lock();
    if (!gfx || !surface_) {
        co_return;
    }

    auto key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
    auto recorder = gfx->AcquireCommandRecorder(key, "SimpleEditorModule");
    if (!recorder) {
        co_return;
    }
    auto back_buffer = surface_->GetCurrentBackBuffer();
    if (!back_buffer) {
        co_return;
    }

    // Create framebuffer descriptor
    graphics::FramebufferDesc fb_desc;
    fb_desc.color_attachments.push_back(graphics::FramebufferAttachment{
        .texture = back_buffer,
        .format = back_buffer->GetDescriptor().format
    });

    // Create framebuffer
    auto fb = gfx->CreateFramebuffer(fb_desc);
    if (!fb) co_return;

    // AcquireCommandRecorder already begins recording and the custom deleter
    // will end/submit when the unique_ptr leaves scope.
    fb->PrepareForRender(*recorder);
    recorder->BindFrameBuffer(*fb);

    // Clear to Cornflower Blue
    // R: 100, G: 149, B: 237 -> 0.392, 0.584, 0.929
    recorder->ClearFramebuffer(*fb,
        std::vector<std::optional<graphics::Color>>{
            graphics::Color(0.392f, 0.584f, 0.929f, 1.0f),
        });

    if (surface_registered_) {
        context.SetSurfacePresentable(surface_index_, true);
    }
    co_return;
}

auto SimpleEditorModule::EnsureSurfaceRegistered(engine::FrameContext& context) -> void
{
    if (!surface_) {
        surface_registered_ = false;
        surface_index_ = std::numeric_limits<size_t>::max();
        return;
    }

    if (surface_registered_) {
        const auto surfaces = context.GetSurfaces();
        if (surface_index_ < surfaces.size()
            && surfaces[surface_index_].get() == surface_.get()) {
            return;
        }
        surface_registered_ = false;
        surface_index_ = std::numeric_limits<size_t>::max();
    }

    auto surfaces = context.GetSurfaces();
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].get() == surface_.get()) {
            surface_registered_ = true;
            surface_index_ = i;
            return;
        }
    }

    const auto insertion_index = surfaces.size();
    context.AddSurface(surface_);
    surface_registered_ = true;
    surface_index_ = insertion_index;
}

} // namespace Oxygen::Editor::EngineInterface
