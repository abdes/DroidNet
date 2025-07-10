// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#pragma once

#include <string>

#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/api_export.h>

namespace oxygen::clap::parser::detail {

OXGN_CLP_API auto UnrecognizedCommand(
  const std::vector<std::string>& path_segments, const char* message = nullptr)
  -> std::string;

OXGN_CLP_API auto MissingCommand(const ParserContextPtr& context,
  const char* message = nullptr) -> std::string;

OXGN_CLP_API auto UnrecognizedOption(const ParserContextPtr& context,
  const std::string& token, const char* message = nullptr) -> std::string;

OXGN_CLP_API auto MissingValueForOption(const ParserContextPtr& context,
  const char* message = nullptr) -> std::string;

OXGN_CLP_API auto InvalidValueForOption(const ParserContextPtr& context,
  const std::string& token, const char* message = nullptr) -> std::string;

OXGN_CLP_API auto IllegalMultipleOccurrence(const ParserContextPtr& context,
  const char* message = nullptr) -> std::string;

OXGN_CLP_API auto OptionSyntaxError(const ParserContextPtr& context,
  const char* message = nullptr) -> std::string;

OXGN_CLP_API auto MissingRequiredOption(const CommandPtr& command,
  const OptionPtr& option, const char* message = nullptr) -> std::string;

OXGN_CLP_API auto UnexpectedPositionalArguments(const ParserContextPtr& context,
  const char* message = nullptr) -> std::string;

} // namespace oxygen::clap::parser::detail
