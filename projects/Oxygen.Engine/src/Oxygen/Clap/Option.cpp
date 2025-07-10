//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>

#include <utility>

#include <Oxygen/TextWrap/TextWrap.h>

namespace oxygen::clap {

ValueSemantics::~ValueSemantics() noexcept = default;

auto operator<<(std::ostream& out, const Option& option) -> std::ostream&
{
  option.Print(out);
  return out;
}

auto Option::PrintValueDescription(
  std::ostream& out, const std::string& separator) const -> void
{
  if (value_semantic_ && !value_semantic_->IsFlag()) {
    out << separator;
    if (!value_semantic_->IsRequired()) {
      out << "[";
    }
    out << "<" << value_semantic_->UserFriendlyName() << ">";
    if (value_semantic_->IsRepeatable()) {
      out << "...";
    }
    if (!value_semantic_->IsRequired()) {
      out << "[";
    }
  }
  out << "\n";
}

auto Option::Print(std::ostream& out, const unsigned int width) const -> void
{
  if (IsPositional()) {
    out << "   ";
    if (!IsRequired()) {
      out << "[";
    }
    out << "<" << UserFriendlyName() << ">";
    if (!IsRequired()) {
      out << "]";
    }
    out << "\n";
  } else {
    if (!short_name_.empty()) {
      out << "   -" << short_name_;
      PrintValueDescription(out, " ");
    }
    if (!long_name_.empty()) {
      out << "   --" << long_name_;
      PrintValueDescription(out, "=");
    }
  }

  const wrap::TextWrapper wrap = wrap::MakeWrapper()
                                   .Width(width)
                                   .CollapseWhiteSpace()
                                   .TrimLines()
                                   .IndentWith()
                                   .Initially("   ")
                                   .Then("   ");
  out << wrap.Fill(About()).value_or("__wrapping_error__");
}

auto Option::WithKey(std::string key) -> OptionBuilder
{
  return OptionBuilder(std::move(key));
}

auto Option::Positional(std::string key) -> PositionalOptionBuilder
{
  return PositionalOptionBuilder(std::move(key));
}

auto Option::Rest() -> PositionalOptionBuilder
{
  return PositionalOptionBuilder(key_rest_);
}

auto operator<<(std::ostream& out, const Options& options) -> std::ostream&
{
  options.Print(out);
  return out;
}

auto Options::Print(std::ostream& out, const unsigned int width) const -> void
{
  if (!label.empty()) {
    out << label << '\n';
  }
  for (const auto& option : options) {
    option->Print(out, width);
  }
}

auto Options::Add(const std::shared_ptr<Option>& option) -> void
{
  options.emplace_back(option);
}

} // namespace oxygen::clap
