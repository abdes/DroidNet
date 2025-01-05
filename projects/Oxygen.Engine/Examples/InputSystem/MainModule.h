//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Module.h>

namespace oxygen::input {
class InputSystem;
}

class MainModule : public oxygen::core::Module
{
 public:
  using Base = Module;

  template <typename... Args>
  explicit MainModule(oxygen::EngineWeakPtr engine, Args&&... ctor_args)
    : Base("MainModule", std::move(engine), std::forward<Args>(ctor_args)...)
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
  struct State {
    float distance { 10.0F };
    float direction { 1.0F };
  };
  State state_;

  std::shared_ptr<oxygen::input::InputSystem> player_input_;

 public:
  [[maybe_unused]] static const char* const LOGGER_NAME;
};
