//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <lua.h>

#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Module/RuntimeDiagnostics.h>

namespace oxygen::scripting::detail {

namespace {

  constexpr std::string_view kParamsPrefix = "self.params.";
  constexpr std::string_view kIndexNilPrefix = "attempt to index nil";

  auto CollectFrames(lua_State* state) -> int
  {
    auto* const frames = static_cast<std::vector<ScriptFrame>*>(
      lua_tolightuserdata(state, lua_upvalueindex(1)));
    if (frames != nullptr) {
      frames->clear();
      lua_Debug info {};
      for (int level = 0; lua_getinfo(state, level, "sln", &info) != 0;
        ++level) {
        const auto source = info.source != nullptr
          ? std::string_view(info.source)
          : std::string_view {};
        if (!source.starts_with('@') || info.currentline <= 0) {
          continue;
        }
        frames->push_back(ScriptFrame {
          .source = std::string(source.substr(1)),
          .line = static_cast<uint32_t>(info.currentline),
          .function = info.name != nullptr ? info.name : "",
        });
      }
    }
    lua_pushvalue(state, 1); // the error object, unchanged
    return 1;
  }

  struct ErrorLocation {
    std::string_view source;
    uint32_t line = 0;
  };

  //! Splits `source:line: message` when `source` is one of the frames.
  auto SplitLocation(std::string_view& message,
    const std::span<const ScriptFrame> frames) -> std::optional<ErrorLocation>
  {
    for (const auto& frame : frames) {
      if (message.size() <= frame.source.size()
        || !message.starts_with(frame.source)
        || message.at(frame.source.size()) != ':') {
        continue;
      }
      const auto rest = message.substr(frame.source.size() + 1);
      auto line = uint32_t { 0 };
      const auto* const first = std::to_address(rest.begin());
      const auto [end, error]
        = std::from_chars(first, std::to_address(rest.end()), line);
      const auto consumed = static_cast<std::size_t>(std::distance(first, end));
      if (error != std::errc {} || !rest.substr(consumed).starts_with(": ")) {
        continue;
      }
      message = rest.substr(consumed + 2);
      return ErrorLocation { .source = frame.source, .line = line };
    }
    return std::nullopt;
  }

  auto IsBlank(const char ch) noexcept -> bool
  {
    return ch == ' ' || ch == '\t';
  }

  //! Span of the code on `line`, without surrounding blanks.
  auto CodeSpan(const ErrorLocation& location,
    const std::optional<std::string_view>& text) -> SourceSpan
  {
    auto span = SourceSpan {
      .source_name = std::string(location.source),
      .begin = { .line = location.line, .column = 1 },
      .end = { .line = location.line, .column = 1 },
    };
    if (!text.has_value()) {
      return span;
    }
    const auto first = std::ranges::find_if_not(*text, IsBlank);
    const auto last
      = std::ranges::find_if_not(text->rbegin(), text->rend(), IsBlank).base();
    if (first < last) {
      span.begin.column
        = static_cast<uint32_t>(std::distance(text->begin(), first)) + 1U;
      span.end.column
        = static_cast<uint32_t>(std::distance(text->begin(), last)) + 1U;
    }
    return span;
  }

  auto IsIdentifierChar(const char ch) noexcept -> bool
  {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
      || (ch >= '0' && ch <= '9') || ch == '_';
  }

  struct UnsetReference {
    std::string name;
    uint32_t begin_column = 0;
    uint32_t end_column = 0;
  };

  //! Finds `self.params.<name>` on `text` naming an unset reference.
  auto FindUnsetReference(const std::string_view text,
    const RuntimeErrorInput& input) -> std::optional<UnsetReference>
  {
    if (!input.is_unset_reference) {
      return std::nullopt;
    }
    for (auto at = text.find(kParamsPrefix); at != std::string_view::npos;
      at = text.find(kParamsPrefix, at + 1)) {
      const auto name_begin = at + kParamsPrefix.size();
      auto name_end = name_begin;
      while (name_end < text.size() && IsIdentifierChar(text.at(name_end))) {
        ++name_end;
      }
      const auto name = text.substr(name_begin, name_end - name_begin);
      if (!name.empty() && input.is_unset_reference(name)) {
        return UnsetReference {
          .name = std::string(name),
          .begin_column = static_cast<uint32_t>(at) + 1U,
          .end_column = static_cast<uint32_t>(name_end) + 1U,
        };
      }
    }
    return std::nullopt;
  }

  auto CallerNote(const ScriptFrame& frame, const bool outermost,
    const std::string_view entry_name) -> std::string
  {
    auto name = std::string("a function");
    if (!frame.function.empty()) {
      name = fmt::format("`{}`", frame.function);
    } else if (outermost && !entry_name.empty()) {
      name = fmt::format("`{}`", entry_name);
    }
    return fmt::format(
      "called from {} at {}:{}", name, frame.source, frame.line);
  }

} // namespace

auto PushErrorFrameCollector(lua_State* state, std::vector<ScriptFrame>* frames)
  -> void
{
  lua_pushlightuserdata(state, frames);
  lua_pushcclosure(state, CollectFrames, "oxygen.error_frames", 1);
}

auto MakeRuntimeDiagnostic(const RuntimeErrorInput& input) -> ScriptDiagnostic
{
  auto message = input.error_message;
  const auto location_in_message = SplitLocation(message, input.frames);
  auto extra_lines = std::string_view {};
  if (const auto line_break = message.find('\n');
    line_break != std::string_view::npos) {
    extra_lines = message.substr(line_break + 1);
    message = message.substr(0, line_break);
  }

  auto diagnostic = ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kRuntimeError,
    .message
    = message.empty() ? std::string("script error") : std::string(message),
    .labels = {},
    .notes = {},
    .help = std::nullopt,
    .slot = std::nullopt,
  };

  auto location = location_in_message;
  if (!location.has_value() && !input.frames.empty()) {
    location = ErrorLocation {
      .source = input.frames.front().source,
      .line = input.frames.front().line,
    };
  }

  auto first_caller = std::size_t { 0 };
  if (location.has_value()) {
    const auto text = input.sources != nullptr
      ? input.sources->Line(location->source, location->line)
      : std::nullopt;
    auto label = DiagnosticLabel {
      .span = CodeSpan(*location, text),
      .message = {},
      .primary = true,
    };
    if (text.has_value() && message.starts_with(kIndexNilPrefix)) {
      if (const auto unset = FindUnsetReference(*text, input)) {
        label.span.begin.column = unset->begin_column;
        label.span.end.column = unset->end_column;
        label.message = "this is nil";
        diagnostic.notes.push_back(
          fmt::format("parameter `{}` is not set on this slot", unset->name));
        diagnostic.help = Suggestion {
          .message = fmt::format(
            "set `{}` in the node's Scripting section", unset->name),
          .span = std::nullopt,
          .replacement = std::nullopt,
        };
      }
    }
    diagnostic.labels.push_back(std::move(label));

    const auto raised = std::ranges::find_if(
      input.frames, [&location](const ScriptFrame& frame) -> bool {
        return frame.source == location->source && frame.line == location->line;
      });
    first_caller = raised == input.frames.end()
      ? 1
      : static_cast<std::size_t>(std::distance(input.frames.begin(), raised))
        + 1;
  }

  if (!extra_lines.empty()) {
    diagnostic.notes.emplace_back(extra_lines);
  }
  if (first_caller < input.frames.size()) {
    const auto callers = input.frames.subspan(first_caller);
    for (const auto& frame : callers) {
      diagnostic.notes.push_back(
        CallerNote(frame, &frame == &callers.back(), input.entry_name));
    }
  }
  return diagnostic;
}

} // namespace oxygen::scripting::detail
