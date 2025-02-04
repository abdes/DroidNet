//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Module.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Platform/Common/Types.h"

class MainModule final : public oxygen::core::Module {
public:
    template <typename... Args>
    explicit MainModule(
        oxygen::PlatformPtr platform,
        oxygen::EngineWeakPtr engine,
        oxygen::platform::WindowPtr window,
        Args&&... ctor_args)
        : Module("MainModule", std::move(engine), std::forward<Args>(ctor_args)...)
        , platform_(std::move(platform))
        , my_window_(std::move(window))
    {
    }

    ~MainModule() override = default;

    OXYGEN_MAKE_NON_COPYABLE(MainModule);
    OXYGEN_MAKE_NON_MOVEABLE(MainModule);

    void OnInitialize(const oxygen::Graphics* gfx) override;

    void ProcessInput(const oxygen::platform::InputEvent& event) override;
    void Update(oxygen::Duration delta_time) override;
    void FixedUpdate() override;
    void Render(const oxygen::Graphics* gfx) override;

    void OnShutdown() noexcept override;

private:
    [[nodiscard]] auto RenderGame(
        const oxygen::Graphics* gfx,
        const oxygen::graphics::RenderTarget& render_target) const
        -> oxygen::graphics::CommandLists;

    oxygen::PlatformPtr platform_ {};

    oxygen::graphics::SurfacePtr surface_ {};
    // TODO: hack for ImGui - redesign surfaces
    oxygen::platform::WindowPtr my_window_ {};
};
