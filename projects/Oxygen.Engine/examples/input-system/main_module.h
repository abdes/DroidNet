//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <oxygen/core/engine.h>
#include <oxygen/core/module.h>
#include <oxygen/input/fwd.h>

class MainModule : public oxygen::core::Module
{
public:
  explicit MainModule(oxygen::Engine& engine);
  ~MainModule() override;

  // Non-copyable
  MainModule(const MainModule&) = delete;
  auto operator=(const MainModule&)->MainModule & = delete;

  // Non-Movable
  MainModule(MainModule&& other) noexcept = delete;
  auto operator=(MainModule&& other) noexcept -> MainModule & = delete;

  void Initialize(const oxygen::Renderer& renderer) override;

  void ProcessInput(const oxygen::platform::InputEvent& event) override;
  void Update(oxygen::Duration delta_time) override;
  void FixedUpdate() override;
  void Render(const oxygen::Renderer& renderer) override;

  void Shutdown() noexcept override;

private:
  struct State
  {
    float distance{ 10.0F };
    float direction{ 1.0F };
  };
  State state_;

  oxygen::Engine& engine_;
  std::shared_ptr<oxygen::input::InputSystem> player_input_;

public:
  [[maybe_unused]] static const char* const LOGGER_NAME;
};
