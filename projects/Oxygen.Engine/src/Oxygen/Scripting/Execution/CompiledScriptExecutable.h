//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

//! Implementation of ScriptExecutable that holds Luau bytecode.
/*!
  CompiledScriptExecutable is the bridge between the compilation service and
  the execution module. It is immutable: a reload installs a new executable in
  the slot (ScriptingComponent::MarkSlotReady), which ends the old instance
  and starts a new one.
*/
class CompiledScriptExecutable final : public ScriptExecutable {
public:
  explicit CompiledScriptExecutable(
    std::shared_ptr<const ScriptBytecodeBlob> bytecode)
    : bytecode_(std::move(bytecode))
  {
  }

  auto Run() const noexcept -> void override { }

  [[nodiscard]] auto BytecodeView() const noexcept
    -> std::span<const uint8_t> override
  {
    return bytecode_ ? bytecode_->BytesView() : std::span<const uint8_t> {};
  }

  [[nodiscard]] auto ContentHash() const noexcept -> uint64_t override
  {
    return bytecode_ ? bytecode_->ContentHash() : 0;
  }

private:
  std::shared_ptr<const ScriptBytecodeBlob> bytecode_;
};

} // namespace oxygen::scripting
