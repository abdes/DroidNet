//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Analysis/DiagnosticCatalog.h>
#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

#include <Scripting/ScriptAnalysis.h>

namespace Oxygen::Interop::Scripting {

namespace {

  namespace native = oxygen::scripting;

  auto ToUtf8(String ^ text) -> std::string
  {
    if (String::IsNullOrEmpty(text)) {
      return {};
    }
    const auto bytes = System::Text::Encoding::UTF8->GetBytes(text);
    const pin_ptr<Byte> pinned = &bytes[0];
    return std::string(reinterpret_cast<const char*>(pinned),
      static_cast<std::size_t>(bytes->Length));
  }

  auto ToManaged(const std::string& text) -> String ^
  {
    if (text.empty()) {
      return String::Empty;
    }
    return gcnew String(text.data(), 0, static_cast<int>(text.size()),
      System::Text::Encoding::UTF8);
  }

  auto ToManaged(const std::optional<double>& value) -> Nullable<double>
  {
    return value.has_value() ? Nullable<double>(*value) : Nullable<double>();
  }

  auto ToManaged(const native::SourceSpan& span) -> ScriptSourceSpan
  {
    auto result = ScriptSourceSpan();
    result.SourceName = ToManaged(span.source_name);
    result.Line = static_cast<int>(span.begin.line);
    result.Column = static_cast<int>(span.begin.column);
    result.EndLine = static_cast<int>(span.end.line);
    result.EndColumn = static_cast<int>(span.end.column);
    return result;
  }

  auto DefaultValue(const oxygen::data::ScriptParam& value) -> Object ^
  {
    if (const auto* flag = std::get_if<bool>(&value)) {
      return *flag;
    }
    if (const auto* integer = std::get_if<int32_t>(&value)) {
      return *integer;
    }
    if (const auto* number = std::get_if<float>(&value)) {
      return *number;
    }
    if (const auto* vec2 = std::get_if<oxygen::Vec2>(&value)) {
      return System::Numerics::Vector2(vec2->x, vec2->y);
    }
    if (const auto* vec3 = std::get_if<oxygen::Vec3>(&value)) {
      return System::Numerics::Vector3(vec3->x, vec3->y, vec3->z);
    }
    if (const auto* vec4 = std::get_if<oxygen::Vec4>(&value)) {
      return System::Numerics::Vector4(vec4->x, vec4->y, vec4->z, vec4->w);
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
      return ToManaged(*text);
    }
    return nullptr;
  }

  auto ToManaged(const native::ScriptParamDeclaration& declaration)
    -> ScriptParameterInfo ^
  {
    auto result = gcnew ScriptParameterInfo();
    result->Name = ToManaged(declaration.name);
    result->Type = static_cast<ScriptParameterType>(declaration.type);
    result->DefaultValue = DefaultValue(declaration.default_value);
    if (const auto* asset = std::get_if<oxygen::data::ScriptAssetParam>(
          &declaration.default_value)) {
      result->AssetType = static_cast<Byte>(asset->asset_type);
    }
    result->Min = ToManaged(declaration.min);
    result->Max = ToManaged(declaration.max);
    result->Step = ToManaged(declaration.step);
    result->Unit = ToManaged(declaration.unit);
    result->Description = ToManaged(declaration.description);
    result->Options = gcnew array<String ^>(
      static_cast<int>(declaration.options.size()));
    for (auto index = 0; index < result->Options->Length; ++index) {
      result->Options[index]
        = ToManaged(declaration.options.at(static_cast<std::size_t>(index)));
    }
    result->IsColor = declaration.color;
    result->Line = static_cast<int>(declaration.span.begin.line);
    result->Column = static_cast<int>(declaration.span.begin.column);
    return result;
  }

  auto ToManaged(const native::ScriptDiagnostic& diagnostic,
    const native::SourceProvider& sources) -> ScriptDiagnosticInfo ^
  {
    auto result = gcnew ScriptDiagnosticInfo();
    result->Severity = static_cast<ScriptDiagnosticSeverity>(diagnostic.severity);
    result->Code = ToManaged(native::to_string(diagnostic.code));
    result->Message = ToManaged(diagnostic.message);
    if (const auto* info = native::Describe(diagnostic.code); info != nullptr) {
      result->Title = ToManaged(std::string(info->title));
      result->Explanation = ToManaged(std::string(info->explanation));
    } else {
      result->Title = String::Empty;
      result->Explanation = String::Empty;
    }
    if (const auto* span = diagnostic.PrimarySpan(); span != nullptr) {
      result->PrimarySpan = Nullable<ScriptSourceSpan>(ToManaged(*span));
    }
    result->Summary = ToManaged(native::FormatSummary(diagnostic));
    result->Rendered = ToManaged(native::RenderText(diagnostic, sources));
    return result;
  }

} // namespace

auto ScriptAnalysis::Analyze(String ^ sourceName, array<Byte> ^ source,
  ScriptAnalysisLevel level, String ^ typeDefinitions) -> ScriptAnalysisResult ^
{
  ArgumentNullException::ThrowIfNull(source, "source");

  const auto name = ToUtf8(sourceName);
  const auto definitions = ToUtf8(typeDefinitions);
  auto text = std::string(static_cast<std::size_t>(source->Length), '\0');
  if (source->Length > 0) {
    const pin_ptr<Byte> pinned = &source[0];
    std::memcpy(text.data(), pinned, text.size());
  }

  const auto bytes = std::as_bytes(std::span(text));
  const auto analysis = native::ScriptAnalyzer().Analyze({
    .source_name = name,
    .source = bytes,
    .level = static_cast<native::AnalysisLevel>(level),
    .type_definitions = definitions,
  });

  auto sources = native::SourceTextCache();
  sources.Add(name, text);

  auto result = gcnew ScriptAnalysisResult();
  result->HasDeclaration = analysis.has_declaration;
  result->Parameters = gcnew array<ScriptParameterInfo ^>(
    static_cast<int>(analysis.declarations.size()));
  for (auto index = 0; index < result->Parameters->Length; ++index) {
    result->Parameters[index]
      = ToManaged(analysis.declarations.at(static_cast<std::size_t>(index)));
  }
  result->Diagnostics = gcnew array<ScriptDiagnosticInfo ^>(
    static_cast<int>(analysis.diagnostics.size()));
  for (auto index = 0; index < result->Diagnostics->Length; ++index) {
    result->Diagnostics[index] = ToManaged(
      analysis.diagnostics.at(static_cast<std::size_t>(index)), sources);
  }
  return result;
}

} // namespace Oxygen::Interop::Scripting
