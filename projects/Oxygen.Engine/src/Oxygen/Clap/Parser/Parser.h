//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap::parser {

class CmdLineParser {
public:
  using CommandsList = const std::vector<Command::Ptr>;
  explicit CmdLineParser(const CommandLineContext& context,
    const Tokenizer& _tokenizer, CommandsList& commands,
    const std::vector<Option::Ptr>& global_options,
    const std::vector<std::pair<Options::Ptr, bool>>& global_option_groups)
    : tokenizer_(_tokenizer)
    , context_ { detail::ParserContext::New(
        context, commands, global_options, global_option_groups) }
  {
  }

  OXGN_CLP_API auto Parse() const -> bool;

private:
  const Tokenizer& tokenizer_;
  detail::ParserContextPtr context_;
};

} // namespace oxygen::clap::parser
