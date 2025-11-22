#pragma once
#pragma managed(push, off)

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <limits>
#include <memory>

namespace Oxygen::Editor::EngineInterface {

class SimpleEditorModule final : public oxygen::engine::EngineModule {
    OXYGEN_TYPED(SimpleEditorModule)
public:
    explicit SimpleEditorModule(std::shared_ptr<oxygen::graphics::Surface> surface);
    ~SimpleEditorModule() override;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override { return "SimpleEditorModule"; }
    [[nodiscard]] auto GetPriority() const noexcept -> oxygen::engine::ModulePriority override { return oxygen::engine::kModulePriorityHighest; }
    [[nodiscard]] auto GetSupportedPhases() const noexcept -> oxygen::engine::ModulePhaseMask override;

    auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool override;
    auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
    auto OnCommandRecord(oxygen::engine::FrameContext& context) -> oxygen::co::Co<> override;

private:
    auto EnsureSurfaceRegistered(oxygen::engine::FrameContext& context) -> void;

    std::shared_ptr<oxygen::graphics::Surface> surface_;
    std::weak_ptr<oxygen::Graphics> graphics_;
    bool surface_registered_ { false };
    size_t surface_index_ { std::numeric_limits<size_t>::max() };
};

} // namespace Oxygen::Editor::EngineInterface

#pragma managed(pop)
