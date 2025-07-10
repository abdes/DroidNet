//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/Clap/api_export.h>

namespace asap::clap::parser {

class CmdLineParser {
public:
  using CommandsList = const std::vector<Command::Ptr>;
  explicit CmdLineParser(const CommandLineContext& context,
    const Tokenizer& tokenizer, CommandsList& commands)
    : tokenizer_(tokenizer)
    , context_ { detail::ParserContext::New(context, commands) }
  {
  }

  OXGN_CLP_API auto Parse() -> bool;

private:
  const Tokenizer& tokenizer_;
  detail::ParserContextPtr context_;
};

} // namespace asap::clap::parser
