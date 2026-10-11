//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include <Oxygen/Scripting/Analysis/DiagnosticCatalog.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

namespace {

  constexpr uint16_t kCodesPerFamily = 100;
  constexpr std::size_t kCodeDigits = 4;

  constexpr auto kCatalog = std::to_array<DiagnosticInfo>({
    {
      .code = DiagnosticCode::kSyntaxError,
      .title = "the script is not valid Luau",
      .explanation
      = "The Luau parser stopped at the marked location. Fix the reported "
        "syntax; nothing after a syntax error is checked.",
    },
    {
      .code = DiagnosticCode::kCompileError,
      .title = "the script could not be compiled",
      .explanation
      = "The script parses but Luau cannot turn it into bytecode, for example "
        "because one function uses more local variables than Luau allows. "
        "Split the marked code into smaller functions.",
    },
    {
      .code = DiagnosticCode::kNoCompiler,
      .title = "no compiler is available for the script language",
      .explanation
      = "The engine has no compiler registered for this script's language, so "
        "its source cannot run. Cook the script with bytecode, or run it in a "
        "host that compiles scripts.",
    },
    {
      .code = DiagnosticCode::kCompileStopped,
      .title = "compilation stopped before it finished",
      .explanation
      = "The compilation service shut down while the script was waiting to "
        "compile. The script itself may be valid; compile it again once the "
        "engine runs.",
    },
    {
      .code = DiagnosticCode::kEmptySource,
      .title = "the script source is empty",
      .explanation
      = "An empty script declares nothing and runs nothing. Write the script "
        "or remove the slot that uses it.",
    },
    {
      .code = DiagnosticCode::kDeclarationNotTopLevelLocal,
      .title = "`oxygen.script` must initialize a top-level local",
      .explanation
      = "Declarations are read without running the script, so the "
        "`oxygen.script { ... }` call must initialize a local at the top of "
        "the chunk, such as `local Orbit = oxygen.script { ... }`.",
    },
    {
      .code = DiagnosticCode::kValueNotLiteral,
      .title = "a declaration value must be a literal",
      .explanation
      = "Declarations are read without running the script, so every field "
        "value must be written out: a number, string, boolean or a table of "
        "them. Replace the computed expression with its value.",
    },
    {
      .code = DiagnosticCode::kDuplicateDeclaration,
      .title = "the script declares `oxygen.script` more than once",
      .explanation
      = "A script has one declaration. Merge the parameters into one "
        "`oxygen.script { ... }` call.",
    },
    {
      .code = DiagnosticCode::kDeclarationNotReturned,
      .title = "the script does not return its declaration",
      .explanation
      = "The chunk must end by returning the local that holds the "
        "`oxygen.script` table, so the engine finds its hooks and parameters.",
    },
    {
      .code = DiagnosticCode::kDeclarationNotTable,
      .title = "a declaration expects a table constructor",
      .explanation
      = "`oxygen.script` and its `params` field take a table written in "
        "place, such as `oxygen.script { params = { speed = "
        "oxygen.param.float { } } }`.",
    },
    {
      .code = DiagnosticCode::kUnknownField,
      .title = "unknown declaration field",
      .explanation
      = "The field is not part of the declaration format. Check its spelling "
        "against the declaration rules or remove it.",
    },
    {
      .code = DiagnosticCode::kParamNotDeclared,
      .title = "a parameter is not declared with `oxygen.param`",
      .explanation
      = "Each entry of `params` must be written `name = "
        "oxygen.param.<type> { ... }` so its type is known without running "
        "the script.",
    },
    {
      .code = DiagnosticCode::kUnknownParamType,
      .title = "unknown parameter type",
      .explanation
      = "Parameter types are `bool`, `int`, `float`, `string`, `vec2`, "
        "`vec3`, `vec4`, `asset` and `node`.",
    },
    {
      .code = DiagnosticCode::kInvalidParamName,
      .title = "invalid parameter name",
      .explanation
      = "Parameter names are stored in cooked slots with a fixed capacity. "
        "Use a shorter name.",
    },
    {
      .code = DiagnosticCode::kDuplicateParam,
      .title = "a parameter is declared twice",
      .explanation
      = "Each parameter name appears once in `params`. Remove or rename one of "
        "them.",
    },
    {
      .code = DiagnosticCode::kDefaultTypeMismatch,
      .title = "a parameter default does not match its type",
      .explanation
      = "The default must be a value of the declared type, for example a "
        "number for `float` or a three-number table for `vec3`.",
    },
    {
      .code = DiagnosticCode::kFieldNotApplicable,
      .title = "a field does not apply to this parameter type",
      .explanation
      = "Some hints only make sense for some types, such as `min` for numbers "
        "or `kind` for assets. Remove the field or change the type.",
    },
    {
      .code = DiagnosticCode::kInvalidAssetKind,
      .title = "invalid asset kind",
      .explanation
      = "An `asset` parameter needs a `kind` naming the asset type it accepts, "
        "such as `material` or `input_action`.",
    },
    {
      .code = DiagnosticCode::kValueTooLong,
      .title = "a declaration string is too long",
      .explanation
      = "Cooked parameter values have a fixed capacity. Shorten the string "
        "default or option.",
    },
    {
      .code = DiagnosticCode::kInvalidRange,
      .title = "invalid parameter range",
      .explanation
      = "`min` must not exceed `max`, `step` must be positive, and the default "
        "must lie within the range and among the `options` when they are "
        "given.",
    },
    {
      .code = DiagnosticCode::kFieldTypeMismatch,
      .title = "a declaration field has the wrong kind of value",
      .explanation
      = "Each declaration field expects one kind of value, such as a string "
        "for `unit` or a number for `step`.",
    },
    {
      .code = DiagnosticCode::kNotAScript,
      .title = "the script does not return its `oxygen.script` table",
      .explanation
      = "A slot runs a chunk that must return the table created by "
        "`oxygen.script`. Return it at the end of the script; a module that "
        "other scripts load cannot be used as a slot script.",
    },
    {
      .code = DiagnosticCode::kLoadFailed,
      .title = "the script bytecode could not be loaded",
      .explanation
      = "The Luau VM rejected the compiled bytecode, usually because it was "
        "produced by an incompatible compiler. Cook the script again.",
    },
    {
      .code = DiagnosticCode::kAttachFailed,
      .title = "the script could not be attached",
      .explanation
      = "A scene names a script whose asset is not loaded, whose source cannot "
        "be found, or that has no bytecode or source to run. The slot stays "
        "disabled; the rest of the scene runs. Check that the script is "
        "cooked with the scene and, for external sources, that the source "
        "file is under a script source root.",
    },
    {
      .code = DiagnosticCode::kOverrideRejected,
      .title = "a parameter override does not match the script",
      .explanation
      = "A scene overrides a parameter the script does not declare, with a "
        "value of another type or asset kind, or with a node the scene does "
        "not have. The override is ignored and the declared default stays. "
        "Update the scene after changing the script's declarations.",
    },
    {
      .code = DiagnosticCode::kRuntimeError,
      .title = "the script raised an error",
      .explanation
      = "A hook raised a Lua error at the marked line. The instance stops and "
        "receives no further hooks until its script changes.",
    },
  });

  // The catalog is ordered so Describe can search it.
  static_assert(std::ranges::is_sorted(kCatalog, {}, &DiagnosticInfo::code));

} // namespace

auto to_string(const DiagnosticFamily value) noexcept -> std::string_view
{
  switch (value) {
  case DiagnosticFamily::kSyntax:
    return "syntax";
  case DiagnosticFamily::kCompile:
    return "compile";
  case DiagnosticFamily::kDeclarations:
    return "declarations";
  case DiagnosticFamily::kLint:
    return "lint";
  case DiagnosticFamily::kTypes:
    return "types";
  case DiagnosticFamily::kBinding:
    return "binding";
  case DiagnosticFamily::kRuntime:
    return "runtime";
  }
  return "__NotSupported__";
}

auto FamilyOf(const DiagnosticCode code) noexcept -> DiagnosticFamily
{
  return static_cast<DiagnosticFamily>(
    static_cast<uint16_t>(code) / kCodesPerFamily);
}

auto DiagnosticCatalog() noexcept -> std::span<const DiagnosticInfo>
{
  return kCatalog;
}

auto Describe(const DiagnosticCode code) noexcept -> const DiagnosticInfo*
{
  const auto it
    = std::ranges::lower_bound(kCatalog, code, {}, &DiagnosticInfo::code);
  return it != kCatalog.end() && it->code == code ? &*it : nullptr;
}

auto ParseDiagnosticCode(const std::string_view text) noexcept
  -> std::optional<DiagnosticCode>
{
  if (text.size() != kCodeDigits + 1 || text.front() != 'S') {
    return std::nullopt;
  }
  const auto digits = text.substr(1);
  auto value = uint16_t { 0 };
  const auto* const last = std::to_address(digits.end());
  const auto [end, error]
    = std::from_chars(std::to_address(digits.begin()), last, value);
  if (error != std::errc {} || end != last) {
    return std::nullopt;
  }
  const auto code = static_cast<DiagnosticCode>(value);
  if (Describe(code) == nullptr) {
    return std::nullopt;
  }
  return code;
}

} // namespace oxygen::scripting
