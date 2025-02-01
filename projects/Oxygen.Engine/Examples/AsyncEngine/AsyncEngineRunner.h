//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Engine.h>
#include <Oxygen/OxCo/Run.h>

namespace oxygen::co {

template <>
struct EventLoopTraits<Engine> {
    static void Run(Engine& engine) { engine.Run(); }
    static void Stop(Engine& engine) { engine.Stop(); }
    static auto IsRunning(const Engine& engine) -> bool { return engine.IsRunning(); }
    static auto EventLoopId(const Engine& engine) -> EventLoopID { return EventLoopID(&engine); }
};

} // namespace oxygen::co
