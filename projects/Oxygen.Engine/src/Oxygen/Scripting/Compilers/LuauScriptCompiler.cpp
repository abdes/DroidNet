//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <string>

#include <Luau/Compiler.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>

namespace oxygen::scripting {

namespace {

  // Luau doesn't expose symbolic constants for these levels in public headers.
  // The documented ranges live in Luau/Compiler.h.
  constexpr int kLuauNoOptimizationLevel = 0;
  constexpr int kLuauMaxOptimizationLevel = 2;
  constexpr int kLuauNoDebugInfoLevel = 0;
  constexpr int kLuauMaxDebugInfoLevel = 2;

  auto ApplyCompileMode(
    Luau::CompileOptions& options, const CompileMode mode) noexcept -> void
  {
    switch (mode) {
    case CompileMode::kDebug:
      options.optimizationLevel = kLuauNoOptimizationLevel;
      options.debugLevel = kLuauMaxDebugInfoLevel;
      return;
    case CompileMode::kOptimized:
      options.optimizationLevel = kLuauMaxOptimizationLevel;
      options.debugLevel = kLuauNoDebugInfoLevel;
      return;
    default:
      options.optimizationLevel = kLuauNoOptimizationLevel;
      options.debugLevel = kLuauMaxDebugInfoLevel;
      return;
    }
  }

} // namespace

auto LuauScriptCompiler::Language() const noexcept -> data::pak::ScriptLanguage
{
  return data::pak::ScriptLanguage::kLuau;
}

auto LuauScriptCompiler::Compile(std::span<const uint8_t> source,
  const CompileMode mode) const -> ScriptCompileResult
{
  LOG_F(INFO, "luau compile begin (source_size={})", source.size());
  if (source.empty()) {
    LOG_F(ERROR, "luau compile rejected empty source");
    return ScriptCompileResult {
      .success = false,
      .bytecode = {},
      .diagnostics = "source is empty",
    };
  }

  try {
    Luau::CompileOptions luau_options {};
    ApplyCompileMode(luau_options, mode);

    std::string source_text;
    source_text.reserve(source.size());
    for (const auto byte : source) {
      source_text.push_back(static_cast<char>(byte));
    }
    const std::string compiled = Luau::compile(source_text, luau_options);
    if (compiled.empty()) {
      return ScriptCompileResult {
        .success = false,
        .bytecode = {},
        .diagnostics = "compiler returned empty output",
      };
    }

    if (compiled.front() == '\0') {
      const std::string diagnostics = compiled.size() > 1
        ? compiled.substr(1)
        : std::string("compile failed without diagnostics");
      DLOG_F(2, "compile failed");
      return ScriptCompileResult {
        .success = false,
        .bytecode = {},
        .diagnostics = diagnostics,
      };
    }

    std::vector<uint8_t> bytecode;
    bytecode.reserve(compiled.size());
    for (const auto ch : compiled) {
      bytecode.push_back(static_cast<uint8_t>(ch));
    }

    DLOG_F(3, "compile succeeded ({} bytes)", bytecode.size());
    return ScriptCompileResult {
      .success = true,
      .bytecode = std::move(bytecode),
      .diagnostics = {},
    };
  } catch (const std::exception& ex) {
    LOG_F(WARNING, "compile threw: {}", ex.what());
    return ScriptCompileResult {
      .success = false,
      .bytecode = {},
      .diagnostics = ex.what(),
    };
  }
}

} // namespace oxygen::scripting
