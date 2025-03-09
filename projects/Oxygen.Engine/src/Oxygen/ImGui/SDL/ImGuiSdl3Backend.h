//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/ImGui/ImGuiPlatformBackend.h>

#include <Oxygen/ImGui/api_export.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen::imgui::sdl3 {

class ImGuiSdl3Backend final : public ImGuiPlatformBackend {
public:
    ImGuiSdl3Backend(
        std::shared_ptr<const Platform> platform,
        const platform::WindowIdType window_id,
        ImGuiContext* imgui_context)
        : ImGuiPlatformBackend("ImGui SDL3 Backend")
        , platform_(std::move(platform))
        , window_id_(window_id)
    {
        OnInitialize(imgui_context);
    }

    OXYGEN_IMGUI_API ~ImGuiSdl3Backend() override
    {
        OnShutdown();
    }

    OXYGEN_MAKE_NON_COPYABLE(ImGuiSdl3Backend);
    OXYGEN_MAKE_NON_MOVABLE(ImGuiSdl3Backend);

    OXYGEN_IMGUI_API void NewFrame() override;

protected:
    void OnInitialize(ImGuiContext* imgui_context) ;
    void OnShutdown() ;

private:
    std::shared_ptr<const Platform> platform_;
    platform::WindowIdType window_id_;
};

} // namespace oxygen
