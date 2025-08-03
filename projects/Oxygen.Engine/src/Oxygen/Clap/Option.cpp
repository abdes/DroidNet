//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/TextWrap/TextWrap.h>

namespace oxygen::clap {

ValueSemantics::~ValueSemantics() noexcept = default;

auto Option::PrintValueDescription(
  const CommandLineContext& context, const std::string& separator) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  if (value_semantic_ && !value_semantic_->IsFlag()) {
    context.out << separator;
    if (!value_semantic_->IsRequired()) {
      context.out << "[";
    }
    context.out << fmt::format(
      theme.placeholder, "<{}>", value_semantic_->UserFriendlyName());
    if (value_semantic_->IsRepeatable()) {
      context.out << "...";
    }
    if (!value_semantic_->IsRequired()) {
      context.out << "[";
    }
  }
  context.out << "\n";
}

auto Option::Print(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  if (IsPositional()) {
    context.out << "   ";
    if (!IsRequired()) {
      context.out << "[";
    }
    context.out << fmt::format(theme.placeholder, "<{}>", UserFriendlyName());
    if (!IsRequired()) {
      context.out << "]";
    }
    context.out << "\n";
  } else {
    if (!short_name_.empty()) {
      context.out << "   "
                  << fmt::format(theme.option_flag, "-{}", short_name_);
      PrintValueDescription(context, " ");
    }
    if (!long_name_.empty()) {
      context.out << "   "
                  << fmt::format(theme.option_flag, "--{}", long_name_);
      PrintValueDescription(context, "=");
    }
  }

  const wrap::TextWrapper wrap = wrap::MakeWrapper()
                                   .Width(width)
                                   .IgnoreAnsiEscapeCodes()
                                   .CollapseWhiteSpace()
                                   .TrimLines()
                                   .IndentWith()
                                   .Initially("   ")
                                   .Then("   ");
  context.out << wrap.Fill(About()).value_or("__wrapping_error__");
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

auto Options::Print(
  const CommandLineContext& context, const unsigned int width) const -> void
{
  const CliTheme& theme = context.theme ? *context.theme : CliTheme::Plain();
  if (!label.empty()) {
    context.out << fmt::format(theme.section_header, "{}", label) << '\n';
  }
  for (const auto& option : options) {
    option->Print(context, width);
  }
}

auto Options::Add(const std::shared_ptr<Option>& option) -> void
{
  options.emplace_back(option);
}

} // namespace oxygen::clap
