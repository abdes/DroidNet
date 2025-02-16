//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "./api_export.h"
#include "Oxygen/Base/Macros.h"
#include "Oxygen/Core/Module.h"
#include "Oxygen/Graphics/Common/Forward.h"
#include "Oxygen/Platform/Types.h"

struct ImGuiContext;
struct ImDrawData;

namespace oxygen::imgui {

class ImGuiRenderInterface;
class ImGuiPlatformBackend;

class ImguiModule : public core::Module {
public:
    using Base = Module;

    template <typename... Args>
    explicit ImguiModule(
        const char* name, EngineWeakPtr engine,
        const platform::WindowIdType window_id,
        Args&&... ctor_args)
        : Base(name, engine, std::forward<Args>(ctor_args)...)
        , window_id_(window_id)
    {
    }

    OXYGEN_IMGUI_API ~ImguiModule() override;

    OXYGEN_MAKE_NON_COPYABLE(ImguiModule);
    OXYGEN_MAKE_NON_MOVABLE(ImguiModule);

    OXYGEN_IMGUI_API auto ProcessInput(const platform::InputEvent& event) -> void override;
    OXYGEN_IMGUI_API auto Update(Duration delta_time) -> void override;
    OXYGEN_IMGUI_API auto FixedUpdate() -> void override;

    OXYGEN_IMGUI_API virtual auto GetRenderInterface() -> ImGuiRenderInterface;

protected:
    OXYGEN_IMGUI_API void OnInitialize(const Graphics* gfx) override;
    OXYGEN_IMGUI_API void OnShutdown() noexcept override;

    OXYGEN_IMGUI_API virtual void ImGuiBackendInit(const Graphics* gfx) = 0;
    OXYGEN_IMGUI_API virtual void ImGuiBackendShutdown() = 0;
    OXYGEN_IMGUI_API virtual void ImGuiBackendNewFrame() = 0;
    OXYGEN_IMGUI_API virtual auto ImGuiBackendRenderRawData(const Graphics* gfx, ImDrawData* draw_data)
        -> graphics::CommandListPtr
        = 0;

    [[nodiscard]] auto GetImGuiContext() const { return imgui_context_; }
    [[nodiscard]] auto GetWindowId() const { return window_id_; }

private:
    friend class ImGuiRenderInterface;
    auto NewFrame(const Graphics* gfx) -> void;
    auto ImGuiRender(const Graphics* gfx) -> graphics::CommandListPtr;

    auto Render(const Graphics* /*gfx*/) -> void override { }

    ImGuiContext* imgui_context_ { nullptr };
    platform::WindowIdType window_id_ {};
    std::shared_ptr<ImGuiPlatformBackend> imgui_platform_ {};
};

} // namespace oxygen
