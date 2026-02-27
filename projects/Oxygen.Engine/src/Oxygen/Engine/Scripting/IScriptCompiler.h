//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Engine/api_export.h>

namespace oxygen::scripting {

enum class CompileMode : uint8_t {
  kDebug,
  kOptimized,
};

[[nodiscard]] constexpr auto to_string(const CompileMode mode) noexcept
  -> std::string_view
{
  switch (mode) {
    // clang-format off
  case CompileMode::kDebug: return "debug";
  case CompileMode::kOptimized: return "optimized";
  default: return "__Unknown__";
    // clang-format on
  }
}

struct ScriptCompileResult {
  bool success { false };
  std::shared_ptr<const ScriptBytecodeBlob> bytecode;
  std::string diagnostics;

  [[nodiscard]] auto HasBytecode() const noexcept -> bool
  {
    return bytecode != nullptr && !bytecode->IsEmpty();
  }
};

class IScriptCompiler {
public:
  IScriptCompiler() = default;
  virtual ~IScriptCompiler() = default;

  OXYGEN_MAKE_NON_COPYABLE(IScriptCompiler)
  OXYGEN_MAKE_NON_MOVABLE(IScriptCompiler)

  [[nodiscard]] virtual auto Language() const noexcept
    -> data::pak::scripting::ScriptLanguage
    = 0;

  [[nodiscard]] virtual auto Compile(
    ScriptSourceBlob source, CompileMode mode) const -> ScriptCompileResult
    = 0;
};

} // namespace oxygen::scripting
