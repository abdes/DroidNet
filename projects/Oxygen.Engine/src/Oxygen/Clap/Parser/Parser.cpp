//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Clap/Parser/Events.h>
#include <Oxygen/Clap/Parser/Parser.h>
#include <Oxygen/Clap/Parser/States.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>

using oxygen::fsm::Continue;
using oxygen::fsm::ReissueEvent;
using oxygen::fsm::Status;
using oxygen::fsm::Terminate;
using oxygen::fsm::TerminateWithError;

using oxygen::clap::parser::detail::DashDashState;
using oxygen::clap::parser::detail::FinalState;
using oxygen::clap::parser::detail::IdentifyCommandState;
using oxygen::clap::parser::detail::InitialState;
using oxygen::clap::parser::detail::Machine;
using oxygen::clap::parser::detail::ParseLongOptionState;
using oxygen::clap::parser::detail::ParseOptionsState;
using oxygen::clap::parser::detail::ParseShortOptionState;
using oxygen::clap::parser::detail::TokenEvent;

/*!
 * \brief The Overload pattern allows to explicitly 'overload' lambdas and is
 * particularly useful for creating visitors, e.g. for std::variant.
 */
template <typename... Ts> // (7)
struct Overload : Ts... {
  using Ts::operator()...;
};
// Deduction guide only needed for C++17. C++20 can automatically create the
// template parameters out of the constructor arguments.
template <class... Ts> Overload(Ts...) -> Overload<Ts...>;

auto oxygen::clap::parser::CmdLineParser::Parse() const -> bool
{
  auto machine = Machine { InitialState { context_ }, IdentifyCommandState {},
    ParseOptionsState {}, ParseShortOptionState {}, ParseLongOptionState {},
    DashDashState {}, FinalState {} };

  bool continue_running { true };
  bool no_errors { true };
  auto token = tokenizer_.NextToken();
  bool reissue = false;
  do {
    const auto& [token_type, token_value] = token;
    DLOG_F(1, "next event: {}/{}", token.first, token.second);

    Status execution_status;

    switch (token_type) {
    case TokenType::kShortOption:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kShortOption> { token_value });
      break;
    case TokenType::kLongOption:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kLongOption> { token_value });
      break;
    case TokenType::kLoneDash:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kLoneDash> { token_value });
      break;
    case TokenType::kDashDash:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kDashDash> { token_value });
      break;
    case TokenType::kEqualSign:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kEqualSign> { token_value });
      break;
    case TokenType::kValue:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kValue> { token_value });
      break;
    case TokenType::kEndOfInput:
      execution_status
        = machine.Handle(TokenEvent<TokenType::kEndOfInput> { token_value });
      break;
    }

    // https://en.cppreference.com/w/cpp/utility/variant/visit
    std::visit(
      Overload {
        [&continue_running](
          const Continue& /*status*/) noexcept { continue_running = true; },
        [&continue_running](
          const Terminate& /*status*/) noexcept { continue_running = false; },
        //  [this, &continue_running, &no_errors, &logger](
        [this, &continue_running, &no_errors](
          const TerminateWithError& status) {
          LOG_F(ERROR, "{}", status.error_message);
          context_->err << fmt::format(
            "{}: {}", context_->program_name, status.error_message)
                        << '\n';
          continue_running = false;
          no_errors = false;
        },
        [&continue_running, &reissue](const ReissueEvent& /*status*/) noexcept {
          reissue = true;
          continue_running = true;
        },
      },
      execution_status);

    if (continue_running) {
      if (reissue) {
        reissue = false;
        // reuse the same token again
        DLOG_F(
          1, "re-issuing event({}/{}) as requested ", token_type, token_value);
      } else {
        DCHECK_NE_F(token.first, TokenType::kEndOfInput);
        token = tokenizer_.NextToken();
      }
    }
  } while (continue_running);
  return no_errors;
}
