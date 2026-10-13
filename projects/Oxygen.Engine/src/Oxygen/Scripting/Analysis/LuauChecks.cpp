//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/LinterConfig.h>
#include <Luau/ParseOptions.h>
#include <Luau/Type.h>
#include <Luau/TypeArena.h>

#include <Oxygen/Scripting/Analysis/DeclarationExtractor.h>
#include <Oxygen/Scripting/Analysis/LuauChecks.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting::detail {

namespace {

  //! Declares the API untyped, so linting alone does not report `oxygen` as
  //! an unknown global.
  constexpr std::string_view kUntypedDefinitions = "declare oxygen: any\n";

  constexpr uint16_t kLintCodeBase = 400;

  // A Luau upgrade that adds lint codes must catalog them (S04xx).
  static_assert(Luau::LintWarning::Code__Count == 30);

  //! Serves the one script being checked; `require` is not followed.
  class SingleSourceResolver final : public Luau::FileResolver {
  public:
    SingleSourceResolver(std::string name, std::string source)
      : name_(std::move(name))
      , source_(std::move(source))
    {
    }

    auto readSource(const Luau::ModuleName& name)
      -> std::optional<Luau::SourceCode> override
    {
      if (name != name_) {
        return std::nullopt;
      }
      return Luau::SourceCode {
        .source = source_,
        .type = Luau::SourceCode::Module,
      };
    }

  private:
    std::string name_;
    std::string source_;
  };

  //! One line, lower case first letter, no trailing period; further lines of
  //! a Luau message become notes.
  auto SplitMessage(const std::string& text)
    -> std::pair<std::string, std::vector<std::string>>
  {
    auto lines = std::vector<std::string> {};
    auto start = std::size_t { 0 };
    while (start <= text.size()) {
      const auto end = text.find('\n', start);
      auto line = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
      while (!line.empty()
        && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
        line.pop_back();
      }
      if (!line.empty()) {
        lines.push_back(std::move(line));
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    if (lines.empty()) {
      return { "type error", {} };
    }
    auto message = std::move(lines.front());
    if (message.size() > 1
      && std::isupper(static_cast<unsigned char>(message[0])) != 0
      && std::islower(static_cast<unsigned char>(message[1])) != 0) {
      message[0] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(message[0])));
    }
    if (!message.empty() && message.back() == '.') {
      message.pop_back();
    }
    lines.erase(lines.begin());
    return { std::move(message), std::move(lines) };
  }

  auto MakeDiagnostic(const std::string_view source_name,
    const DiagnosticSeverity severity, const DiagnosticCode code,
    const std::string& text, const Luau::Location& location) -> ScriptDiagnostic
  {
    auto [message, notes] = SplitMessage(text);
    return ScriptDiagnostic {
      .severity = severity,
      .code = code,
      .message = std::move(message),
      .labels = {
        DiagnosticLabel {
          .span = ToSpan(source_name, location),
          .message = {},
          .primary = true,
        },
      },
      .notes = std::move(notes),
      .help = std::nullopt,
      .slot = std::nullopt,
    };
  }

  struct TypeErrorKind {
    DiagnosticCode code = DiagnosticCode::kTypeError;
    DiagnosticSeverity severity = DiagnosticSeverity::kError;
  };

  auto Classify(const Luau::TypeError& error) -> TypeErrorKind
  {
    using Luau::get;
    if (get<Luau::TypeMismatch>(error) != nullptr
      || get<Luau::TypePackMismatch>(error) != nullptr) {
      return { .code = DiagnosticCode::kTypeMismatch };
    }
    if (get<Luau::UnknownSymbol>(error) != nullptr) {
      return { .code = DiagnosticCode::kUnknownGlobal };
    }
    if (get<Luau::UnknownProperty>(error) != nullptr
      || get<Luau::UnknownPropButFoundLikeProp>(error) != nullptr
      || get<Luau::MissingUnionProperty>(error) != nullptr) {
      return { .code = DiagnosticCode::kUnknownMember };
    }
    if (get<Luau::CountMismatch>(error) != nullptr) {
      return { .code = DiagnosticCode::kArgumentCountMismatch };
    }
    if (get<Luau::CannotCallNonFunction>(error) != nullptr) {
      return { .code = DiagnosticCode::kNotCallable };
    }
    if (get<Luau::OptionalValueAccess>(error) != nullptr) {
      return { .code = DiagnosticCode::kOptionalValueAccess };
    }
    if (get<Luau::DeprecatedApiUsed>(error) != nullptr) {
      return {
        .code = DiagnosticCode::kDeprecatedApi,
        .severity = DiagnosticSeverity::kWarning,
      };
    }
    if (get<Luau::UnknownRequire>(error) != nullptr
      || get<Luau::IllegalRequire>(error) != nullptr) {
      return {
        .code = DiagnosticCode::kUnsupportedRequire,
        .severity = DiagnosticSeverity::kWarning,
      };
    }
    if (get<Luau::CodeTooComplex>(error) != nullptr
      || get<Luau::UnificationTooComplex>(error) != nullptr
      || get<Luau::NormalizationTooComplex>(error) != nullptr) {
      return {
        .code = DiagnosticCode::kTypeCheckIncomplete,
        .severity = DiagnosticSeverity::kWarning,
      };
    }
    return {};
  }

  auto LintCode(const Luau::LintWarning::Code code) -> DiagnosticCode
  {
    if (code <= Luau::LintWarning::Code_Unknown
      || code >= Luau::LintWarning::Code__Count) {
      return DiagnosticCode::kLintUnknown;
    }
    return static_cast<DiagnosticCode>(
      kLintCodeBase + static_cast<uint16_t>(code));
  }

  auto AddApiHelp(ScriptDiagnostic& diagnostic) -> void
  {
    if (diagnostic.code == DiagnosticCode::kUnknownMember
      || diagnostic.code == DiagnosticCode::kUnknownGlobal) {
      diagnostic.help = Suggestion {
        .message = "check the spelling against the Oxygen API in "
                   "`Types/oxygen.d.luau`",
        .span = std::nullopt,
        .replacement = std::nullopt,
      };
    }
  }

} // namespace

auto RunLuauChecks(const LuauCheckRequest& request,
  std::vector<ScriptDiagnostic>& diagnostics) -> void
{
  const auto check_types
    = request.check_types && !request.type_definitions.empty();
  auto module_name = std::string(request.source_name);
  auto resolver
    = SingleSourceResolver(module_name, std::string(request.source));

  auto config_resolver = Luau::NullConfigResolver {};
  config_resolver.defaultConfig.mode = Luau::Mode::Nonstrict;
  config_resolver.defaultConfig.enabledLint.setDefaults();
  if (check_types) {
    // The type checker reports unknown globals with their types.
    config_resolver.defaultConfig.enabledLint.disableWarning(
      Luau::LintWarning::Code_UnknownGlobal);
  }

  auto options = Luau::FrontendOptions {};
  options.runLintChecks = true;
  auto frontend = Luau::Frontend(
    Luau::SolverMode::Old, &resolver, &config_resolver, options);
  Luau::registerBuiltinGlobals(frontend, frontend.globals);

  const auto definitions
    = check_types ? request.type_definitions : kUntypedDefinitions;
  const auto loaded = frontend.loadDefinitionFile(frontend.globals,
    frontend.globals.globalScope, definitions, "@oxygen", false, false);
  Luau::freeze(frontend.globals.globalTypes);
  if (!loaded.success) {
    auto diagnostic = ScriptDiagnostic {
      .severity = DiagnosticSeverity::kWarning,
      .code = DiagnosticCode::kTypeCheckIncomplete,
      .message = "the oxygen type definitions could not be loaded",
      .labels = {},
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    };
    for (const auto& error : loaded.parseResult.errors) {
      diagnostic.notes.push_back(error.getMessage());
    }
    if (loaded.module != nullptr) {
      for (const auto& error : loaded.module->errors) {
        diagnostic.notes.push_back(Luau::toString(error));
      }
    }
    diagnostics.push_back(std::move(diagnostic));
    return;
  }

  const auto result = frontend.check(module_name);

  for (const auto& warning : result.lintResult.errors) {
    diagnostics.push_back(
      MakeDiagnostic(request.source_name, DiagnosticSeverity::kError,
        LintCode(warning.code), warning.text, warning.location));
  }
  for (const auto& warning : result.lintResult.warnings) {
    diagnostics.push_back(
      MakeDiagnostic(request.source_name, DiagnosticSeverity::kWarning,
        LintCode(warning.code), warning.text, warning.location));
  }

  if (!check_types) {
    return;
  }
  for (const auto& error : result.errors) {
    if (error.moduleName != module_name) {
      continue;
    }
    const auto kind = Classify(error);
    auto diagnostic = MakeDiagnostic(request.source_name, kind.severity,
      kind.code, Luau::toString(error), error.location);
    AddApiHelp(diagnostic);
    diagnostics.push_back(std::move(diagnostic));
  }
  if (!result.timeoutHits.empty()) {
    diagnostics.push_back(ScriptDiagnostic {
      .severity = DiagnosticSeverity::kWarning,
      .code = DiagnosticCode::kTypeCheckIncomplete,
      .message = "type checking timed out",
      .labels = {},
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    });
  }
}

} // namespace oxygen::scripting::detail
