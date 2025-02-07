//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Core/Types.h"
#include "Oxygen/Core/api_export.h"
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Base/TimeUtils.h>

namespace oxygen {

class Graphics;

namespace platform {
    class InputEvent;
} // namespace platform

namespace core {

    class Module : public Composition {
    public:
        OXYGEN_CORE_API Module(std::string_view name, EngineWeakPtr engine);

        OXYGEN_CORE_API ~Module() override;

        OXYGEN_MAKE_NON_COPYABLE(Module)
        OXYGEN_MAKE_NON_MOVEABLE(Module)

        [[nodiscard]] auto Name() const -> std::string_view;

        virtual auto ProcessInput(const platform::InputEvent& event) -> void = 0;
        virtual auto Update(Duration delta_time) -> void = 0;
        virtual auto FixedUpdate() -> void = 0;
        virtual auto Render(const Graphics* gfx) -> void = 0;

        OXYGEN_CORE_API void Initialize(const Graphics* gfx);
        OXYGEN_CORE_API void Shutdown() noexcept;

    protected:
        virtual void OnInitialize(const Graphics* gfx) = 0;
        virtual void OnShutdown() noexcept = 0;
        [[nodiscard]] auto GetEngine() const -> const Engine& { return *(engine_.lock()); }

    private:
        EngineWeakPtr engine_;
        bool is_initialized_ { false };
    };

} // namespace core

} // namespace core
