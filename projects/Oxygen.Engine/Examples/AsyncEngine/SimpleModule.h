//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

class SimpleModule {
public:
    explicit SimpleModule()
    {
    }

    ~SimpleModule() = default;

    OXYGEN_MAKE_NON_COPYABLE(SimpleModule)
    OXYGEN_MAKE_NON_MOVEABLE(SimpleModule)

    void OnInitialize() { }

    void ProcessInput(/*const oxygen::platform::InputEvent& event*/) { }
    void Update(/*oxygen::Duration delta_time*/) { }
    void FixedUpdate() { }
    void Render(/*const oxygen::Graphics* gfx*/) { }

    void OnShutdown() noexcept { }
};
