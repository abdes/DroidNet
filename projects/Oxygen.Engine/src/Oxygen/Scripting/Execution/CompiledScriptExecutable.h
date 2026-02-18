//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class CompiledScriptExecutable final : public ScriptExecutable {
public:
  explicit CompiledScriptExecutable(
    std::shared_ptr<const ScriptBytecodeBlob> bytecode)
    : bytecode_(std::move(bytecode))
  {
  }

  auto Run() const noexcept -> void override { }

  [[nodiscard]] auto Bytecode() const noexcept -> std::span<const uint8_t>
  {
    return bytecode_ != nullptr ? bytecode_->BytesView()
                                : std::span<const uint8_t> {};
  }

private:
  std::shared_ptr<const ScriptBytecodeBlob> bytecode_;
};

} // namespace oxygen::scripting
