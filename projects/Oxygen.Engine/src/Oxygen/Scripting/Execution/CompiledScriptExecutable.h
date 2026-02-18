//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class CompiledScriptExecutable final : public ScriptExecutable {
public:
  explicit CompiledScriptExecutable(std::vector<uint8_t> bytecode)
    : bytecode_(std::move(bytecode))
  {
  }

  auto Run() const noexcept -> void override { }

  [[nodiscard]] auto Bytecode() const noexcept -> std::span<const uint8_t>
  {
    return bytecode_;
  }

private:
  std::vector<uint8_t> bytecode_;
};

} // namespace oxygen::scripting
