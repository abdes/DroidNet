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
#include <set>
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
    if (diagnostic.slot.has_value()) {
      result->SlotNode = ToManaged(diagnostic.slot->node);
      result->SlotIndex
        = Nullable<int>(static_cast<int>(diagnostic.slot->slot_index));
      result->SlotHook = ToManaged(diagnostic.slot->hook);
    }
    return result;
  }

  using System::Text::Json::JsonElement;
  using System::Text::Json::JsonValueKind;

  // The JSON form is `oxygen.script-diagnostic.schema.json`; a field of the
  // wrong kind reads as missing, which leaves the diagnostic unrendered.
  auto TryGet(JsonElement element, String ^ name, JsonElement % value)
    -> bool
  {
    return element.ValueKind == JsonValueKind::Object
      && element.TryGetProperty(name, value);
  }

  auto ReadString(JsonElement element, String ^ name)
    -> std::optional<std::string>
  {
    auto value = JsonElement();
    if (!TryGet(element, name, value)
      || value.ValueKind != JsonValueKind::String) {
      return std::nullopt;
    }
    return ToUtf8(value.GetString());
  }

  auto ReadUnsigned(JsonElement element, String ^ name)
    -> std::optional<uint32_t>
  {
    auto value = JsonElement();
    auto number = UInt32();
    if (!TryGet(element, name, value)
      || value.ValueKind != JsonValueKind::Number
      || !value.TryGetUInt32(number)) {
      return std::nullopt;
    }
    return static_cast<uint32_t>(number);
  }

  auto ReadPosition(JsonElement element, String ^ name)
    -> std::optional<native::SourcePosition>
  {
    auto position = JsonElement();
    if (!TryGet(element, name, position)) {
      return std::nullopt;
    }
    const auto line = ReadUnsigned(position, "line");
    const auto column = ReadUnsigned(position, "column");
    if (!line.has_value() || !column.has_value()) {
      return std::nullopt;
    }
    return native::SourcePosition { .line = *line, .column = *column };
  }

  auto ReadSpan(JsonElement element, String ^ name)
    -> std::optional<native::SourceSpan>
  {
    auto span = JsonElement();
    if (!TryGet(element, name, span)) {
      return std::nullopt;
    }
    auto source = ReadString(span, "source");
    const auto begin = ReadPosition(span, "begin");
    const auto end = ReadPosition(span, "end");
    if (!source.has_value() || !begin.has_value() || !end.has_value()) {
      return std::nullopt;
    }
    return native::SourceSpan {
      .source_name = std::move(*source),
      .begin = *begin,
      .end = *end,
    };
  }

  auto ReadSeverity(const std::string& text)
    -> std::optional<native::DiagnosticSeverity>
  {
    if (text == "error") {
      return native::DiagnosticSeverity::kError;
    }
    if (text == "warning") {
      return native::DiagnosticSeverity::kWarning;
    }
    if (text == "note") {
      return native::DiagnosticSeverity::kNote;
    }
    return std::nullopt;
  }

  auto ReadLabels(JsonElement root, native::ScriptDiagnostic& diagnostic)
    -> bool
  {
    auto labels = JsonElement();
    if (!TryGet(root, "labels", labels)
      || labels.ValueKind != JsonValueKind::Array) {
      return false;
    }
    for (auto index = 0; index < labels.GetArrayLength(); ++index) {
      auto label = labels[index];
      auto span = ReadSpan(label, "span");
      auto message = ReadString(label, "message");
      auto primary = JsonElement();
      if (!span.has_value() || !message.has_value()
        || !TryGet(label, "primary", primary)
        || (primary.ValueKind != JsonValueKind::True
          && primary.ValueKind != JsonValueKind::False)) {
        return false;
      }
      diagnostic.labels.push_back({
        .span = std::move(*span),
        .message = std::move(*message),
        .primary = primary.GetBoolean(),
      });
    }
    return true;
  }

  auto ReadNotes(JsonElement root, native::ScriptDiagnostic& diagnostic)
    -> bool
  {
    auto notes = JsonElement();
    if (!TryGet(root, "notes", notes)
      || notes.ValueKind != JsonValueKind::Array) {
      return false;
    }
    for (auto index = 0; index < notes.GetArrayLength(); ++index) {
      auto note = notes[index];
      if (note.ValueKind != JsonValueKind::String) {
        return false;
      }
      diagnostic.notes.push_back(ToUtf8(note.GetString()));
    }
    return true;
  }

  auto ReadHelpAndSlot(
    JsonElement root, native::ScriptDiagnostic& diagnostic) -> void
  {
    auto help = JsonElement();
    if (TryGet(root, "help", help)) {
      if (auto message = ReadString(help, "message"); message.has_value()) {
        diagnostic.help = native::Suggestion {
          .message = std::move(*message),
          .span = ReadSpan(help, "span"),
          .replacement = ReadString(help, "replacement"),
        };
      }
    }
    auto slot = JsonElement();
    if (TryGet(root, "slot", slot)) {
      auto node = ReadString(slot, "node");
      const auto index = ReadUnsigned(slot, "slot_index");
      auto hook = ReadString(slot, "hook");
      if (node.has_value() && index.has_value() && hook.has_value()) {
        diagnostic.slot = native::SlotContext {
          .node = std::move(*node),
          .slot_index = *index,
          .hook = std::move(*hook),
        };
      }
    }
  }

  auto ReadDiagnostic(JsonElement root)
    -> std::optional<native::ScriptDiagnostic>
  {
    const auto severity_text = ReadString(root, "severity");
    const auto code_text = ReadString(root, "code");
    auto message = ReadString(root, "message");
    if (!severity_text.has_value() || !code_text.has_value()
      || !message.has_value()) {
      return std::nullopt;
    }
    const auto severity = ReadSeverity(*severity_text);
    const auto code = native::ParseDiagnosticCode(*code_text);
    if (!severity.has_value() || !code.has_value()) {
      return std::nullopt;
    }
    auto diagnostic = native::ScriptDiagnostic {
      .severity = *severity,
      .code = *code,
      .message = std::move(*message),
    };
    if (!ReadLabels(root, diagnostic) || !ReadNotes(root, diagnostic)) {
      return std::nullopt;
    }
    ReadHelpAndSlot(root, diagnostic);
    return diagnostic;
  }

  //! Loads the text of every source the diagnostic's spans name.
  auto LoadSources(const native::ScriptDiagnostic& diagnostic,
    Func<String ^, String ^> ^ readSource) -> native::SourceTextCache
  {
    auto names = std::set<std::string> {};
    for (const auto& label : diagnostic.labels) {
      names.insert(label.span.source_name);
    }
    if (diagnostic.help.has_value() && diagnostic.help->span.has_value()) {
      names.insert(diagnostic.help->span->source_name);
    }
    auto sources = native::SourceTextCache();
    for (const auto& name : names) {
      if (String ^ text = readSource(ToManaged(name)); text != nullptr) {
        sources.Add(name, ToUtf8(text));
      }
    }
    return sources;
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

auto ScriptAnalysis::Render(String ^ json,
  Func<String ^, String ^> ^ readSource) -> ScriptDiagnosticInfo ^
{
  ArgumentNullException::ThrowIfNull(json, "json");
  ArgumentNullException::ThrowIfNull(readSource, "readSource");

  System::Text::Json::JsonDocument ^ document = nullptr;
  try {
    document = System::Text::Json::JsonDocument::Parse(
      json, System::Text::Json::JsonDocumentOptions());
    const auto diagnostic = ReadDiagnostic(document->RootElement);
    if (!diagnostic.has_value()) {
      return nullptr;
    }
    return ToManaged(*diagnostic, LoadSources(*diagnostic, readSource));
  } catch (System::Text::Json::JsonException ^) {
    return nullptr;
  } finally {
    delete document;
  }
}

} // namespace Oxygen::Interop::Scripting
