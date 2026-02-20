//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

//! Implementation of ScriptExecutable that holds Luau bytecode.
/*!
  CompiledScriptExecutable acts as the bridge between the compilation service
  and the execution module. It stores the compiled bytecode and provides
  atomic, thread-safe access to it.

  ### Design Contracts
  - **Hot-Reloadable**: Supports atomic replacement of its bytecode via
    UpdateBytecode(). This is the primary mechanism for script hot-reloading.
  - **Thread-Safe**: Uses std::atomic<shared_ptr> to ensure that script
    execution on the main thread is never interrupted or corrupted by
    background bytecode updates.
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
    if (auto blob = bytecode_.load(std::memory_order_acquire)) {
      return blob->BytesView();
    }
    return {};
  }

  [[nodiscard]] auto ContentHash() const noexcept -> uint64_t override
  {
    if (auto blob = bytecode_.load(std::memory_order_acquire)) {
      return blob->ContentHash();
    }
    return 0;
  }

  auto UpdateBytecode(std::shared_ptr<const ScriptBytecodeBlob> new_bytecode)
    -> void
  {
    bytecode_.store(std::move(new_bytecode), std::memory_order_release);
  }

private:
  std::atomic<std::shared_ptr<const ScriptBytecodeBlob>> bytecode_;
};

} // namespace oxygen::scripting
