//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>

namespace oxygen::engine {
class EngineModule;
} // namespace oxygen::engine

namespace oxygen::engine {

// Minimal event payload for module lifecycle notifications.
struct ModuleEvent {
  TypeId type_id;
  std::string name;
  observer_ptr<EngineModule> module;
};

using ModuleAttachedCallback = std::function<void(ModuleEvent const&)>;

} // namespace oxygen::engine
