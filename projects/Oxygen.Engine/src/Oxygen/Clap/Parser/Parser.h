//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap::parser {

//! Parses a command line into a `CommandLineContext`.
/*!
 The parse runs on a context of its own, built from `context`, and writes the
 active command and the option values it found back into `context` when it
 ends, whether it succeeded or not. `context` must outlive the parser.
*/
class CmdLineParser {
public:
  using CommandsList = const std::vector<Command::Ptr>;
  explicit CmdLineParser(CommandLineContext& context,
    const Tokenizer& tokenizer, CommandsList& commands,
    const std::vector<Option::Ptr>& global_options)
    : tokenizer_(&tokenizer)
    , result_(&context)
    , context_ {
      detail::ParserContext::New(context, commands, global_options),
    }
  {
  }

  //! Parses the whole command line; returns whether it had no errors.
  OXGN_CLP_API auto Parse() -> bool;

private:
  [[nodiscard]] auto Run() const -> bool;

  observer_ptr<const Tokenizer> tokenizer_;
  observer_ptr<CommandLineContext> result_;
  detail::ParserContextPtr context_;
};

} // namespace oxygen::clap::parser
