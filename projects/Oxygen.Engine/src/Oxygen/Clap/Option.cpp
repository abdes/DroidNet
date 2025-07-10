//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>

#include <utility>

#include <Oxygen/TextWrap/TextWrap.h>

namespace asap::clap {

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

void Option::Print(std::ostream& out, unsigned int width) const
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
  out << wrap.Fill(About()).value();
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
  return PositionalOptionBuilder(key_rest);
}

auto operator<<(std::ostream& out, const Options& options) -> std::ostream&
{
  options.Print(out);
  return out;
}

void Options::Print(std::ostream& out, unsigned int width) const
{
  if (!label_.empty()) {
    out << label_ << std::endl;
  }
  for (const auto& option : options_) {
    option->Print(out, width);
  }
}

void Options::Add(const std::shared_ptr<Option>& option)
{
  options_.emplace_back(option);
}

} // namespace asap::clap
