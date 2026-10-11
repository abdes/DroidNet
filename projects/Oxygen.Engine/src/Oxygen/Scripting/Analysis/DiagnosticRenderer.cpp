//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting {

namespace {

  constexpr std::size_t kTabWidth = 4;
  constexpr unsigned char kUtf8ContinuationMask = 0xC0U;
  constexpr unsigned char kUtf8ContinuationBits = 0x80U;

  //! Display columns of `text`: code points, with tabs as four columns.
  auto DisplayWidth(const std::string_view text) -> std::size_t
  {
    auto width = std::size_t { 0 };
    for (const auto ch : text) {
      const auto byte = static_cast<unsigned char>(ch);
      if (ch == '\t') {
        width += kTabWidth;
      } else if ((byte & kUtf8ContinuationMask) != kUtf8ContinuationBits) {
        ++width;
      }
    }
    return width;
  }

  auto ExpandTabs(const std::string_view text) -> std::string
  {
    auto expanded = std::string {};
    expanded.reserve(text.size());
    for (const auto ch : text) {
      if (ch == '\t') {
        expanded.append(kTabWidth, ' ');
      } else {
        expanded.push_back(ch);
      }
    }
    return expanded;
  }

  auto TrimRight(std::string_view text) -> std::string_view
  {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
      text.remove_suffix(1);
    }
    return text;
  }

  //! Display column of 1-based byte column `column` on `line`.
  auto DisplayColumn(const std::string_view line, const uint32_t column)
    -> std::size_t
  {
    const auto bytes
      = std::min<std::size_t>(column > 0 ? column - 1U : 0U, line.size());
    return DisplayWidth(line.substr(0, bytes));
  }

  auto IsMultiLine(const SourceSpan& span) noexcept -> bool
  {
    return span.end.line > span.begin.line;
  }

  auto DigitCount(uint32_t value) noexcept -> std::size_t
  {
    auto digits = std::size_t { 1 };
    constexpr uint32_t kDecimalBase = 10;
    while (value >= kDecimalBase) {
      value /= kDecimalBase;
      ++digits;
    }
    return digits;
  }

  auto Location(const SourceSpan& span) -> std::string
  {
    return fmt::format(
      "{}:{}:{}", span.source_name, span.begin.line, span.begin.column);
  }

  //! Labels of one source, rendered as one excerpt.
  struct Excerpt {
    std::string_view source_name;
    std::vector<const DiagnosticLabel*> labels;
    //! The span drawn in the margin, if any.
    const DiagnosticLabel* margin = nullptr;
  };

  auto GroupBySource(const ScriptDiagnostic& diagnostic,
    const SourceSpan* location) -> std::vector<Excerpt>
  {
    auto excerpts = std::vector<Excerpt> {};
    if (location != nullptr) {
      excerpts.push_back(Excerpt {
        .source_name = location->source_name,
        .labels = {},
        .margin = nullptr,
      });
    }
    for (const auto& label : diagnostic.labels) {
      auto it = std::ranges::find(excerpts,
        std::string_view(label.span.source_name), &Excerpt::source_name);
      if (it == excerpts.end()) {
        excerpts.push_back(Excerpt {
          .source_name = label.span.source_name,
          .labels = {},
          .margin = nullptr,
        });
        it = std::prev(excerpts.end());
      }
      it->labels.push_back(&label);
      if (it->margin == nullptr && IsMultiLine(label.span)) {
        it->margin = &label;
      }
    }
    return excerpts;
  }

  class TextRenderer final {
  public:
    TextRenderer(const ScriptDiagnostic& diagnostic,
      const SourceProvider& sources, const std::size_t gutter_width)
      : diagnostic_(&diagnostic)
      , sources_(&sources)
      , width_(gutter_width)
    {
    }

    auto EmptyGutter() -> void
    {
      out_ += fmt::format("{:{}}|\n", "", width_ + 1);
    }

    //! Appends the excerpt; returns false when no line of it is available.
    auto RenderExcerpt(const Excerpt& excerpt) -> bool
    {
      const auto lines = AvailableLines(excerpt);
      if (lines.empty()) {
        return false;
      }
      EmptyGutter();
      auto previous = uint32_t { 0 };
      for (const auto& [number, text] : lines) {
        if (previous != 0 && number > previous + 1) {
          out_ += "...\n";
        }
        RenderLine(excerpt, number, text);
        previous = number;
      }
      return true;
    }

    auto RenderNotes() -> void
    {
      if (const auto& slot = diagnostic_->slot; slot.has_value()) {
        Note("note",
          fmt::format("in {}, node \"{}\", slot {}", slot->hook, slot->node,
            slot->slot_index + 1));
      }
      for (const auto& note : diagnostic_->notes) {
        Note("note", note);
      }
      if (diagnostic_->help.has_value()) {
        Note("help", diagnostic_->help->message);
      }
    }

    auto Append(const std::string_view text) -> void { out_ += text; }

    [[nodiscard]] auto HasNotes() const noexcept -> bool
    {
      return diagnostic_->slot.has_value() || !diagnostic_->notes.empty()
        || diagnostic_->help.has_value();
    }

    [[nodiscard]] auto Take() -> std::string { return std::move(out_); }

  private:
    auto Note(const std::string_view kind, const std::string_view text) -> void
    {
      out_ += fmt::format("{:{}}= {}: {}\n", "", width_ + 1, kind, text);
    }

    [[nodiscard]] auto AvailableLines(const Excerpt& excerpt) const
      -> std::vector<std::pair<uint32_t, std::string_view>>
    {
      auto numbers = std::set<uint32_t> {};
      for (const auto* label : excerpt.labels) {
        if (label == excerpt.margin) {
          for (auto line = label->span.begin.line; line <= label->span.end.line;
            ++line) {
            numbers.insert(line);
          }
        } else {
          numbers.insert(label->span.begin.line);
        }
      }
      auto lines = std::vector<std::pair<uint32_t, std::string_view>> {};
      for (const auto number : numbers) {
        if (const auto text = sources_->Line(excerpt.source_name, number)) {
          lines.emplace_back(number, *text);
        }
      }
      return lines;
    }

    //! Margin text of `line`: `/ ` opens the margin span, `| ` continues it.
    static auto MarginOf(const Excerpt& excerpt, const uint32_t line,
      const bool underline_row) -> std::string_view
    {
      if (excerpt.margin == nullptr) {
        return {};
      }
      const auto& span = excerpt.margin->span;
      if (!underline_row && line == span.begin.line) {
        return "/ ";
      }
      const auto inside = underline_row
        ? line >= span.begin.line && line < span.end.line
        : line > span.begin.line && line <= span.end.line;
      return inside ? "| " : "  ";
    }

    auto RenderLine(const Excerpt& excerpt, const uint32_t number,
      const std::string_view text) -> void
    {
      out_ += TrimRight(fmt::format("{:>{}} | {}{}", number, width_,
        MarginOf(excerpt, number, false), ExpandTabs(text)));
      out_ += '\n';

      auto on_line = std::vector<const DiagnosticLabel*> {};
      for (const auto* label : excerpt.labels) {
        if (label != excerpt.margin && label->span.begin.line == number) {
          on_line.push_back(label);
        }
      }
      std::ranges::sort(
        on_line, {}, [](const DiagnosticLabel* label) -> uint32_t {
          return label->span.begin.column;
        });
      for (const auto* label : on_line) {
        RenderUnderline(excerpt, number, text, *label);
      }
      if (excerpt.margin != nullptr
        && excerpt.margin->span.end.line == number) {
        RenderMarginEnd(text, *excerpt.margin);
      }
    }

    auto RenderUnderline(const Excerpt& excerpt, const uint32_t number,
      const std::string_view text, const DiagnosticLabel& label) -> void
    {
      const auto& span = label.span;
      const auto start = DisplayColumn(text, span.begin.column);
      const auto stop = IsMultiLine(span)
        ? DisplayWidth(text)
        : DisplayColumn(text, span.end.column);
      const auto count
        = std::max<std::size_t>(1, stop > start ? stop - start : 0);
      auto row = fmt::format("{:{}}| {}{:{}}{}", "", width_ + 1,
        MarginOf(excerpt, number, true), "", start,
        std::string(count, label.primary ? '^' : '-'));
      if (!label.message.empty()) {
        row += ' ';
        row += label.message;
      }
      out_ += row;
      out_ += '\n';
    }

    auto RenderMarginEnd(
      const std::string_view text, const DiagnosticLabel& label) -> void
    {
      const auto end = DisplayColumn(text, label.span.end.column);
      auto row = fmt::format("{:{}}| |{}{}", "", width_ + 1,
        std::string(std::max<std::size_t>(end, 1), '_'),
        label.primary ? '^' : '-');
      if (!label.message.empty()) {
        row += ' ';
        row += label.message;
      }
      out_ += row;
      out_ += '\n';
    }

    observer_ptr<const ScriptDiagnostic> diagnostic_;
    observer_ptr<const SourceProvider> sources_;
    std::size_t width_;
    std::string out_;
  };

  auto GutterWidth(const ScriptDiagnostic& diagnostic) -> std::size_t
  {
    auto largest = uint32_t { 0 };
    for (const auto& label : diagnostic.labels) {
      largest
        = std::max({ largest, label.span.begin.line, label.span.end.line });
    }
    return DigitCount(largest);
  }

} // namespace

auto SourceTextCache::Add(std::string source_name, std::string text) -> void
{
  auto source = Source { .text = std::move(text), .line_starts = { 0 } };
  for (std::size_t i = 0; i < source.text.size(); ++i) {
    if (source.text.at(i) == '\n') {
      source.line_starts.push_back(i + 1);
    }
  }
  sources_.insert_or_assign(std::move(source_name), std::move(source));
}

auto SourceTextCache::Line(const std::string_view source_name,
  const uint32_t line) const -> std::optional<std::string_view>
{
  const auto it = sources_.find(source_name);
  if (it == sources_.end() || line == 0) {
    return std::nullopt;
  }
  const auto& source = it->second;
  const auto index = std::size_t { line - 1U };
  if (index >= source.line_starts.size()) {
    return std::nullopt;
  }
  const auto begin = source.line_starts.at(index);
  const auto end = index + 1 < source.line_starts.size()
    ? source.line_starts.at(index + 1) - 1
    : source.text.size();
  auto view = std::string_view(source.text).substr(begin, end - begin);
  if (view.ends_with('\r')) {
    view.remove_suffix(1);
  }
  return view;
}

auto RenderText(const ScriptDiagnostic& diagnostic,
  const SourceProvider& sources) -> std::string
{
  const auto width = GutterWidth(diagnostic);
  auto renderer = TextRenderer(diagnostic, sources, width);
  renderer.Append(fmt::format("{}[{}]: {}\n", to_string(diagnostic.severity),
    to_string(diagnostic.code), diagnostic.message));

  const auto* location = diagnostic.PrimarySpan();
  if (location == nullptr && !diagnostic.labels.empty()) {
    location = &diagnostic.labels.front().span;
  }

  auto excerpts_rendered = false;
  if (location != nullptr) {
    renderer.Append(
      fmt::format("{:{}}--> {}\n", "", width, Location(*location)));
    const auto excerpts = GroupBySource(diagnostic, location);
    for (std::size_t i = 0; i < excerpts.size(); ++i) {
      const auto& excerpt = excerpts.at(i);
      if (i > 0 && !excerpt.labels.empty()) {
        renderer.Append(fmt::format(
          "{:{}}::: {}\n", "", width, Location(excerpt.labels.front()->span)));
      }
      excerpts_rendered = renderer.RenderExcerpt(excerpt) || excerpts_rendered;
    }
  }

  if (renderer.HasNotes() && excerpts_rendered) {
    renderer.EmptyGutter();
  }
  renderer.RenderNotes();
  return renderer.Take();
}

} // namespace oxygen::scripting
