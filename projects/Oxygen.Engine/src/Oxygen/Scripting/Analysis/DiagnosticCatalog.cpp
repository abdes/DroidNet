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
      .code = DiagnosticCode::kLintUnknown,
      .title = "the linter reported a problem",
      .explanation
      = "The Luau linter flagged the marked code with a warning this engine "
        "version does not catalog. Read the message and fix the code.",
    },
    {
      .code = DiagnosticCode::kLintUnknownGlobal,
      .title = "unknown global",
      .explanation
      = "The name is neither a local nor a known global, so it reads nil at "
        "run time. Check its spelling or declare it with `local`.",
    },
    {
      .code = DiagnosticCode::kLintDeprecatedGlobal,
      .title = "deprecated global",
      .explanation
      = "The global still works but is deprecated. Use the replacement the "
        "message names.",
    },
    {
      .code = DiagnosticCode::kLintGlobalUsedAsLocal,
      .title = "a global is used like a local",
      .explanation
      = "The global is written and read in one function only, which usually "
        "means a missing `local`. Declare it as a local.",
    },
    {
      .code = DiagnosticCode::kLintLocalShadow,
      .title = "a local shadows another variable",
      .explanation
      = "The new local hides a variable of the same name from an outer scope. "
        "Rename one of them so the code reads the variable you mean.",
    },
    {
      .code = DiagnosticCode::kLintSameLineStatement,
      .title = "two statements on one line",
      .explanation
      = "A statement follows another on the same line, which often hides a "
        "missing operator or line break. Put each statement on its own line.",
    },
    {
      .code = DiagnosticCode::kLintMultiLineStatement,
      .title = "a statement continues on a misleading line",
      .explanation
      = "The statement continues on the next line without indentation, so it "
        "reads like two statements. Indent the continuation.",
    },
    {
      .code = DiagnosticCode::kLintLocalUnused,
      .title = "unused local",
      .explanation
      = "The local is assigned but never read. Remove it, or name it with a "
        "leading `_` when it is kept on purpose.",
    },
    {
      .code = DiagnosticCode::kLintFunctionUnused,
      .title = "unused local function",
      .explanation
      = "The local function is never called. Remove it, or name it with a "
        "leading `_` when it is kept on purpose.",
    },
    {
      .code = DiagnosticCode::kLintImportUnused,
      .title = "unused import",
      .explanation
      = "The value loaded with `require` is never used. Remove the import.",
    },
    {
      .code = DiagnosticCode::kLintBuiltinGlobalWrite,
      .title = "a builtin global is overwritten",
      .explanation
      = "Assigning to a builtin global such as `math` or `oxygen` replaces it "
        "for the whole script. Use a local with another name.",
    },
    {
      .code = DiagnosticCode::kLintPlaceholderRead,
      .title = "the placeholder `_` is read",
      .explanation
      = "`_` names values that are ignored. Give the value a real name if the "
        "code reads it.",
    },
    {
      .code = DiagnosticCode::kLintUnreachableCode,
      .title = "unreachable code",
      .explanation
      = "The marked code runs after a `return`, `break` or error and never "
        "executes. Remove it or fix the control flow above it.",
    },
    {
      .code = DiagnosticCode::kLintUnknownType,
      .title = "unknown type name in a `type` or `typeof` check",
      .explanation
      = "The string compared with `type()` or `typeof()` is not a name these "
        "functions return, so the comparison is always false.",
    },
    {
      .code = DiagnosticCode::kLintForRange,
      .title = "a numeric `for` loop does not run as written",
      .explanation
      = "The loop bounds and step make the loop run zero times or count the "
        "wrong way. Check the start, end and step.",
    },
    {
      .code = DiagnosticCode::kLintUnbalancedAssignment,
      .title = "an assignment has more names or values on one side",
      .explanation
      = "Extra names receive nil and extra values are dropped. Make both "
        "sides of the assignment the same length.",
    },
    {
      .code = DiagnosticCode::kLintImplicitReturn,
      .title = "a function returns nothing on some paths",
      .explanation
      = "The function returns values on some paths and falls off the end on "
        "others. Return a value on every path.",
    },
    {
      .code = DiagnosticCode::kLintDuplicateLocal,
      .title = "a local is declared twice in one statement",
      .explanation
      = "The same name appears twice in one `local` statement or parameter "
        "list; only the last one is visible. Rename one of them.",
    },
    {
      .code = DiagnosticCode::kLintFormatString,
      .title = "invalid format string",
      .explanation
      = "The format string passed to `string.format` or a similar function "
        "is malformed. Fix the format specifiers.",
    },
    {
      .code = DiagnosticCode::kLintTableLiteral,
      .title = "a table literal repeats a key",
      .explanation
      = "The key appears more than once in the table constructor; only the "
        "last value is kept. Remove the duplicate.",
    },
    {
      .code = DiagnosticCode::kLintUninitializedLocal,
      .title = "a local is read before it is assigned",
      .explanation
      = "The local is declared without a value and read before any "
        "assignment, so it reads nil. Assign it first.",
    },
    {
      .code = DiagnosticCode::kLintDuplicateFunction,
      .title = "a function is defined twice",
      .explanation
      = "The second definition replaces the first. Remove or rename one of "
        "them.",
    },
    {
      .code = DiagnosticCode::kLintDeprecatedApi,
      .title = "deprecated API",
      .explanation
      = "The member is deprecated. Use the replacement the message names.",
    },
    {
      .code = DiagnosticCode::kLintTableOperations,
      .title = "a table operation is likely wrong",
      .explanation
      = "The call to `table.insert`, `table.remove` or a similar function "
        "uses an index or value that is probably a mistake, such as index 0.",
    },
    {
      .code = DiagnosticCode::kLintDuplicateCondition,
      .title = "a condition is repeated",
      .explanation
      = "The same condition appears twice in an `if` chain or a logical "
        "expression, so one branch can never run. Fix the condition.",
    },
    {
      .code = DiagnosticCode::kLintMisleadingAndOr,
      .title = "an `and`/`or` expression can return the wrong value",
      .explanation
      = "`a and b or c` returns `c` when `b` is false or nil. Use an `if` "
        "expression instead.",
    },
    {
      .code = DiagnosticCode::kLintCommentDirective,
      .title = "unknown comment directive",
      .explanation
      = "A `--!` comment names a directive Luau does not know. Check its "
        "spelling, such as `--!strict`.",
    },
    {
      .code = DiagnosticCode::kLintIntegerParsing,
      .title = "a number literal is out of range",
      .explanation
      = "The integer literal cannot be represented exactly. Write a smaller "
        "value or a floating-point literal.",
    },
    {
      .code = DiagnosticCode::kLintComparisonPrecedence,
      .title = "a comparison is grouped unexpectedly",
      .explanation
      = "`not a == b` compares `not a` with `b`. Add parentheses to say which "
        "comparison you mean.",
    },
    {
      .code = DiagnosticCode::kLintRedundantNativeAttribute,
      .title = "redundant `@native` attribute",
      .explanation
      = "The function is already compiled natively by a `--!native` "
        "directive. Remove the attribute.",
    },
    {
      .code = DiagnosticCode::kTypeMismatch,
      .title = "a value has the wrong type",
      .explanation
      = "The value's type does not match what the code expects, for example "
        "a string passed where an Oxygen function takes a number.",
    },
    {
      .code = DiagnosticCode::kUnknownGlobal,
      .title = "unknown global",
      .explanation
      = "The name is neither a local, a Luau builtin nor part of the Oxygen "
        "API. Check its spelling or declare it with `local`.",
    },
    {
      .code = DiagnosticCode::kUnknownMember,
      .title = "unknown member",
      .explanation
      = "The value has no member with this name, for example a misspelled "
        "`oxygen.scene` function. Check the spelling against the Oxygen API; "
        "the project's `Types/oxygen.d.luau` lists every member.",
    },
    {
      .code = DiagnosticCode::kArgumentCountMismatch,
      .title = "wrong number of arguments or values",
      .explanation
      = "A call passes more or fewer arguments than the function takes, or a "
        "function returns a different number of values than expected.",
    },
    {
      .code = DiagnosticCode::kNotCallable,
      .title = "the value is not a function",
      .explanation
      = "The code calls a value whose type is not a function. Check that the "
        "name refers to the function you mean.",
    },
    {
      .code = DiagnosticCode::kOptionalValueAccess,
      .title = "a value that can be nil is used without a check",
      .explanation
      = "The value may be nil, for example a node that was not found, and "
        "the code indexes or calls it. Check it for nil first.",
    },
    {
      .code = DiagnosticCode::kDeprecatedApi,
      .title = "deprecated API",
      .explanation
      = "The member is deprecated. Use the replacement the message names.",
    },
    {
      .code = DiagnosticCode::kUnsupportedRequire,
      .title = "the required module cannot be checked",
      .explanation
      = "Scripts are checked one at a time, so the type checker does not "
        "follow `require`. The module's value is treated as `any`.",
    },
    {
      .code = DiagnosticCode::kTypeError,
      .title = "type error",
      .explanation
      = "The Luau type checker found the problem the message describes. "
        "Scripts are checked in non-strict mode unless they start with "
        "`--!strict`.",
    },
    {
      .code = DiagnosticCode::kTypeCheckIncomplete,
      .title = "the type checker gave up on the script",
      .explanation
      = "The script is too complex or took too long to check, so some type "
        "errors may be missing. Split large functions or tables.",
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
