//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/ImGui/ImGuiPlatformBackend.h"

#include "Oxygen/Platform/Types.h"
#include "Oxygen/Platform/SDL/api_export.h"

namespace oxygen::imgui::sdl3 {

class ImGuiSdl3Backend final : public ImGuiPlatformBackend {
public:
    template <typename... Args>
    explicit ImGuiSdl3Backend(
        std::shared_ptr<const Platform> platform,
        const platform::WindowIdType window_id,
        Args&&... args)
        : ImGuiPlatformBackend("ImGui SDL3 Backend", std::forward<Args>(args)...)
        , platform_(std::move(platform))
        , window_id_(window_id)
    {
    }

    OXYGEN_SDL3_API ~ImGuiSdl3Backend() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ImGuiSdl3Backend);
    OXYGEN_MAKE_NON_MOVEABLE(ImGuiSdl3Backend);

    OXYGEN_SDL3_API void NewFrame() override;

protected:
    void OnInitialize(ImGuiContext* imgui_context) override;
    void OnShutdown() override;

private:
    std::shared_ptr<const Platform> platform_;
    platform::WindowIdType window_id_;
};

} // namespace oxygen
