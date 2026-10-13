//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

namespace oxygen::control {
class CommandDispatcher;
} // namespace oxygen::control

namespace oxygen::examples {

//! The commands a panel's controls run.
/*!
 A panel control that changes settings or state runs a command, so a remote
 client can do what a person does and sees what the person did as a
 `command` event. Layout and widget presentation stay local to the panel.

 Each `Add*` registers a command whose single parameter is `value` (`Color`
 takes `r g b a`, `Rgb` takes `r g b`, `Vec3` takes `x y z`); a successful
 command replies with the value it set. The matching `Run*` binds its
 arguments through the registry, so a control sees the same validation as
 the console and remote clients, and posts the invocation with the `ui`
 source. The host's dispatcher starts it at the next frame start, before the
 panels draw again; without a dispatcher (unit tests) it runs at once.

 Commands registered here are unregistered when the object is destroyed, so
 it must be destroyed before the objects its handlers use.
*/
class PanelCommands final {
public:
  PanelCommands(observer_ptr<commands::CommandRegistry> registry,
    observer_ptr<control::CommandDispatcher> dispatcher);
  ~PanelCommands();

  OXYGEN_MAKE_NON_COPYABLE(PanelCommands)
  OXYGEN_MAKE_NON_MOVABLE(PanelCommands)

  //! Register a command; throws when the registry rejects it.
  auto Add(commands::CommandDefinition definition) -> void;

  auto AddAction(std::string name, std::string label, std::string help,
    std::function<void()> action) -> void;
  auto AddBool(std::string name, std::string label, std::string help,
    std::function<void(bool)> set) -> void;
  auto AddInt(std::string name, std::string label, std::string help,
    std::function<void(int)> set, std::optional<int> min = std::nullopt,
    std::optional<int> max = std::nullopt) -> void;
  auto AddFloat(std::string name, std::string label, std::string help,
    std::function<void(float)> set, std::optional<float> min = std::nullopt,
    std::optional<float> max = std::nullopt) -> void;
  //! `set` receives the index of the value in `values`.
  auto AddEnum(std::string name, std::string label, std::string help,
    std::vector<std::string> values, std::function<void(std::size_t)> set)
    -> void;
  auto AddText(std::string name, std::string label, std::string help,
    std::function<void(const std::string&)> set) -> void;
  auto AddColor(std::string name, std::string label, std::string help,
    std::function<void(const graphics::Color&)> set) -> void;
  //! A linear color without alpha: `r`, `g` and `b`, each at least zero.
  auto AddRgb(std::string name, std::string label, std::string help,
    std::function<void(const glm::vec3&)> set,
    std::optional<float> max = std::nullopt) -> void;
  //! `x`, `y` and `z`, each within the optional limits.
  auto AddVec3(std::string name, std::string label, std::string help,
    std::function<void(const glm::vec3&)> set,
    std::optional<float> min = std::nullopt,
    std::optional<float> max = std::nullopt) -> void;

  //! Post `name` with its arguments as a JSON object.
  auto Run(std::string_view name, const nlohmann::json& arguments = {}) const
    -> void;
  auto RunBool(std::string_view name, bool value) const -> void;
  auto RunInt(std::string_view name, std::int64_t value) const -> void;
  auto RunFloat(std::string_view name, double value) const -> void;
  auto RunText(std::string_view name, std::string_view value) const -> void;
  auto RunColor(std::string_view name, const graphics::Color& value) const
    -> void;
  auto RunVec3(std::string_view name, const glm::vec3& value) const -> void;
  auto RunRgb(std::string_view name, const glm::vec3& value) const -> void;

  [[nodiscard]] auto Registry() const noexcept
    -> observer_ptr<commands::CommandRegistry>
  {
    return registry_;
  }

private:
  auto AddTriple(std::string name, std::string label, std::string help,
    std::array<const char*, 3> components,
    std::function<void(const glm::vec3&)> set, std::optional<float> min,
    std::optional<float> max) -> void;

  observer_ptr<commands::CommandRegistry> registry_;
  observer_ptr<control::CommandDispatcher> dispatcher_;
  std::vector<commands::CommandHandle> handles_;
};

} // namespace oxygen::examples
