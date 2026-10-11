//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <optional>
#include <string>

#include <Luau/Allocator.h>
#include <Luau/BytecodeBuilder.h>
#include <Luau/Compiler.h>
#include <Luau/Lexer.h>
#include <Luau/ParseResult.h>
#include <Luau/Parser.h>

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Scripting/Analysis/DeclarationExtractor.h>
#include <Oxygen/Scripting/Analysis/LuauCompiler.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

namespace {

  using core::meta::scripting::ScriptCompileMode;

  // Luau documents these levels in Luau/Compiler.h but exposes no constants.
  constexpr int kNoOptimization = 0;
  constexpr int kFullOptimization = 2;
  constexpr int kNoDebugInfo = 0;
  constexpr int kFullDebugInfo = 2;

  auto MakeCompileOptions(const ScriptCompileMode mode) noexcept
    -> Luau::CompileOptions
  {
    auto options = Luau::CompileOptions {};
    if (mode == ScriptCompileMode::kOptimized) {
      options.optimizationLevel = kFullOptimization;
      options.debugLevel = kNoDebugInfo;
    } else {
      options.optimizationLevel = kNoOptimization;
      options.debugLevel = kFullDebugInfo;
    }
    return options;
  }

} // namespace

auto CompileLuau(const LuauCompileInput& input) -> LuauCompileOutput
{
  auto output = LuauCompileOutput {};
  if (input.source.empty()) {
    output.diagnostics.push_back(ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = DiagnosticCode::kEmptySource,
      .message = "the script source is empty",
      .labels = {
        DiagnosticLabel {
          .span = SourceSpan {
            .source_name = std::string(input.source_name),
            .begin = {},
            .end = {},
          },
          .message = {},
          .primary = true,
        },
      },
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    });
    return output;
  }

  auto source = std::string(input.source.size(), '\0');
  std::memcpy(source.data(), input.source.data(), input.source.size());

  auto allocator = Luau::Allocator {};
  auto names = Luau::AstNameTable(allocator);
  const auto parsed
    = Luau::Parser::parse(source.data(), source.size(), names, allocator);
  if (!parsed.errors.empty()) {
    for (const auto& error : parsed.errors) {
      output.diagnostics.push_back(
        detail::ToSyntaxDiagnostic(input.source_name, error));
    }
    return output;
  }

  try {
    auto builder = Luau::BytecodeBuilder {};
    Luau::compileOrThrow(
      builder, parsed, names, MakeCompileOptions(input.mode));
    const auto& bytecode = builder.getBytecode();
    output.bytecode.resize(bytecode.size());
    std::memcpy(output.bytecode.data(), bytecode.data(), bytecode.size());
  } catch (const Luau::CompileError& error) {
    output.diagnostics.push_back(ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = DiagnosticCode::kCompileError,
      .message = error.what(),
      .labels = {
        DiagnosticLabel {
          .span = detail::ToSpan(input.source_name, error.getLocation()),
          .message = {},
          .primary = true,
        },
      },
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    });
  }
  return output;
}

} // namespace oxygen::scripting
