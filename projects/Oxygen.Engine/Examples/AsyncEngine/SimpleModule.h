//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Module.h>

class SimpleModule final : public oxygen::core::Module {
public:
    explicit SimpleModule(oxygen::EngineWeakPtr engine)
        : Module("SimpleModule", std::move(engine))
    {
    }

    ~SimpleModule() override = default;

    OXYGEN_MAKE_NON_COPYABLE(SimpleModule)
    OXYGEN_MAKE_NON_MOVEABLE(SimpleModule)

    void OnInitialize(const oxygen::Graphics* /*gfx*/) override { }

    void ProcessInput(const oxygen::platform::InputEvent& /*event*/) override { }
    void Update(oxygen::Duration /*delta_time*/) override { }
    void FixedUpdate() override { }
    void Render(const oxygen::Graphics* /*gfx*/) override { }

    void OnShutdown() noexcept override { }
};
