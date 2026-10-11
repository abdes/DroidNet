//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Engine/Scripting/IScriptCompiler.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scripting/Analysis/LuauCompiler.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>

namespace oxygen::scripting {

auto LuauScriptCompiler::Language() const noexcept
  -> data::pak::scripting::ScriptLanguage
{
  return data::pak::scripting::ScriptLanguage::kLuau;
}

auto LuauScriptCompiler::Compile(ScriptSourceBlob source,
  const core::meta::scripting::ScriptCompileMode mode) const
  -> ScriptCompileResult
{
  const auto& source_name = source.GetCanonicalName().get();
  DLOG_F(2, "luau compile begin (source={}, size={})", source_name,
    source.BytesView().size());
  auto output = CompileLuau(LuauCompileInput {
    .source_name = source_name,
    .source = std::as_bytes(source.BytesView()),
    .mode = mode,
  });
  if (!output.Succeeded()) {
    return ScriptCompileResult {
      .success = false,
      .bytecode = nullptr,
      .diagnostics = std::move(output.diagnostics),
    };
  }

  auto bytecode = std::vector<uint8_t>(output.bytecode.size());
  std::ranges::transform(
    output.bytecode, bytecode.begin(), [](const std::byte value) -> uint8_t {
      return std::to_integer<uint8_t>(value);
    });
  DLOG_F(3, "luau compile succeeded ({} bytes)", bytecode.size());
  return ScriptCompileResult {
    .success = true,
    .bytecode = std::make_shared<const ScriptBytecodeBlob>(
      ScriptBytecodeBlob::FromOwned(std::move(bytecode), Language(),
        data::pak::scripting::ScriptCompression::kNone, 0,
        ScriptBlobOrigin::kEmbeddedResource,
        ScriptBlobCanonicalName { "compiled:luau" })),
    .diagnostics = {},
  };
}

} // namespace oxygen::scripting
