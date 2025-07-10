//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause).
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Internal/Errors.h>
#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/Parser/Events.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>

namespace asap::clap::parser::detail {

using asap::fsm::ByDefault;
using asap::fsm::Continue;
using asap::fsm::DoNothing;
using asap::fsm::Maybe;
using asap::fsm::On;
using asap::fsm::OneOf;
using asap::fsm::ReissueEvent;
using asap::fsm::ReportError;
using asap::fsm::StateMachine;
using asap::fsm::Status;
using asap::fsm::Terminate;
using asap::fsm::TerminateWithError;
using asap::fsm::TransitionTo;
using asap::fsm::Will;

struct InitialState;
struct IdentifyCommandState;
struct ParseOptionsState;
struct ParseShortOptionState;
struct ParseLongOptionState;
struct DashDashState;
struct FinalState;

/*!
 * \brief A short alias for the state machine type used for the command line
 * arguments parser.
 */
using Machine
  = StateMachine<InitialState, IdentifyCommandState, ParseOptionsState,
    ParseShortOptionState, ParseLongOptionState, DashDashState, FinalState>;

/*!
 * \brief The initial state of the parser's state machine.
 *
 * When the parser is created, its starting state is automatically set to the
 * `InitialState` created with the list of commands supported by the CLI.
 *
 * **Parser context**:
 *
 *  - If the CLI has a default command, this will be the starting
 *  `active_command` for the parser.
 *
 * **Transitions**:
 *
 * - ParseOptionsState:
 *   - if the current token is a `TokenType::Value` and it does not match the
 *     initial segment of any of the supported commands.
 *   - if the current token is a `TokenType::ShortOption` or
 *     `TokenType::LongOption` or `TokenType::LoneDash` and the CLI has a
 *     default command.
 * - IdentifyCommandState: if the current token is a `TokenType::Value` and it
 *   matches the initial segment of one of the supported commands.
 * - DashDashState: if the current token is a `TokenType::DashDash` and the CLI
 *   has a default command.
 * - FinalState: if the current token is a `TokenType::EndOfInput` and the CLI
 *   has a default command.
 *
 * **Errors**:
 *
 * - UnrecognizedCommand: if the current token is a `TokenType::Value` that does
 *   not match the initial segment of any of the supported commands and the CLI
 *   does not have a default command.
 * - MissingCommand: if the current token is not a `TokenType::Value` and the
 *   CLI does not have a default command.
 */
struct InitialState {

  explicit InitialState(ParserContextPtr context)
    : context_ { std::move(context) }
  {
    for (const auto& command : context_->commands) {
      if (command->IsDefault()) {
        context_->active_command = command;
        break;
      }
    }
  }

  auto Handle(const TokenEvent<TokenType::Value>& event) -> OneOf<ReportError,
    TransitionTo<ParseOptionsState>, TransitionTo<IdentifyCommandState>>
  {
    // We have a token that could be either a command path segment or a value.
    // If we can find at least one command which path starts with the token then
    // we are sure this is the start of a command path. Otherwise, this can only
    // be a value, and we must have a default command.
    if (MaybeCommand(event.token)) {
      return TransitionTo<IdentifyCommandState>(context_);
    }
    if (context_->active_command) {
      return TransitionTo<ParseOptionsState>(context_);
    }
    return ReportError(UnrecognizedCommand({ event.token }));
  }

  auto Handle(const TokenEvent<TokenType::EndOfInput>& /*event*/)
    -> OneOf<ReportError, TransitionTo<FinalState>>
  {
    if (context_->active_command) {
      return TransitionTo<FinalState>(context_);
    }
    return ReportError(MissingCommand(context_));
  }

  template <TokenType token_type>
  auto Handle(const TokenEvent<token_type>& /*event*/)
    -> OneOf<TransitionTo<ParseOptionsState>, TransitionTo<FinalState>,
      TransitionTo<DashDashState>, ReportError>
  {
    static_assert(token_type != TokenType::Value);
    // For any token type other than TokenType::Value, we require a default
    // command to be present.
    if (context_->active_command) {
      if constexpr (token_type == TokenType::DashDash) {
        return TransitionTo<DashDashState>(context_);
      } else {
        return TransitionTo<ParseOptionsState>(context_);
      }
    }
    return ReportError(MissingCommand(context_));
  }

  [[nodiscard]] auto context() const -> const ParserContextPtr&
  {
    return context_;
  }

private:
  [[nodiscard]] auto MaybeCommand(const std::string& token) const -> bool
  {
    return std::any_of(std::cbegin(context_->commands),
      std::cend(context_->commands), [&token](const auto& command) {
        return (!command->Path().empty() && command->Path()[0] == token);
      });
  }

  ParserContextPtr context_;
};

/*!
 * \brief The parser's state while trying to identify the command (if any)
 * present in the command line.
 *
 * This state is entered only from the InitialState when the current token
 * matches the initial segments of one of the supported commands.
 *
 * **Parser context**:
 *
 * - Upon entering, it is expected that a `ParserContext` object is passed as
 *   `data` to the `OnEnter()` handler, and that such context contains a non
 *   empty list of commands.
 * - Before leaving, this state will ensure that the context's `active_command`
 *   field contains the deepest match of a supported command if possible. For
 *   example, a command line interface that supports commands with paths `do`
 *   and `do it`, presented with arguments `"do it"` will match the deepest
 *   command, i.e. `do it` and not the command `do` with a value argument.
 * - If the arguments do not match any command, but the CLI has a default
 *   command, the `active_command` will be the default command. Otherwise, the
 *   state terminates with error.
 * - When no match has been found but the CLI has a default command, any token
 *   previously collected while attempting to match a command will be considered
 *   as positional arguments and will be added in the order they were
 *   encountered to the context's `positional_tokens`.
 *
 * **Transitions**:
 *
 * - IdentifyCommandState: as long as tokens encountered are TokenType::Value
 *   and they incrementally match path segments of known commands, the state
 *   machine will stay at the same state.
 * - ParseOptionsState: if tokens previously collected match a known command or
 *   the CLI has a default one, and the current token is not a
 *   `TokenType::EndOfInput` or `TokenType::DashDash` and will not produce a
 *   deeper match.
 * - DashDashState: if tokens previously collected match a known command or the
 *   CLI has a default one, and the current token is a `TokenType::DashDash`.
 * - FinalState: if tokens previously collected match a known command or the CLI
 *   has a default one, and the current token is a `TokenType::EndOfInput`.
 *
 * **Errors**:
 *
 * - UnrecognizedCommand: if there was no match so far for any of the known
 *   commands and the CLI does not have a default command.
 */
struct IdentifyCommandState {

  auto OnEnter(const TokenEvent<TokenType::Value>& event, std::any data)
    -> Status
  {
    // Entering here, we have the assurance that the token already matches (at
    // least partially), one of the commands. We'll now start narrowing down the
    // matches by identifying and tracking any full match and by filtering the
    // rest of the commands to only keep those with partial match.
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    DCHECK_F(!Commands().empty());

    path_segments_.push_back(event.token);

    const auto commands_begin = Commands().cbegin();
    const auto commands_end = Commands().cend();
    std::copy_if(commands_begin, commands_end,
      std::back_inserter(filtered_commands_),
      [this, &event](const CommandPtr& command) {
        if (command->IsDefault()) {
          default_command_ = command;
        }
        if (command->Path().empty() || command->Path()[0] != event.token) {
          return false;
        }
        if (command->Path().size() == 1) {
          last_matched_command_ = command;
        }
        return true;
      });
    DCHECK_F(!filtered_commands_.empty());
    return Continue {};
  }

  template <TokenType token_type>
  auto OnLeave(const TokenEvent<token_type>& /*event*/) -> Status
  {
    // Reset the state in case we re-enter again later
    Reset();
    return Continue {};
  }

  template <TokenType token_type>
  auto Handle(const TokenEvent<token_type>& /*event*/)
    -> OneOf<TransitionTo<ParseOptionsState>, TransitionTo<DashDashState>,
      TransitionTo<FinalState>, ReportError>
  {
    // Protect against calling `Handle` without a prior call to `Enter`
    DCHECK_F(!path_segments_.empty());

    if (last_matched_command_) {
      context_->active_command = last_matched_command_;
      switch (token_type) {
      case TokenType::DashDash:
        return TransitionTo<DashDashState>(context_);
      case TokenType::EndOfInput:
        return TransitionTo<FinalState>(context_);
      case TokenType::LongOption:
      case TokenType::ShortOption:
      case TokenType::LoneDash:
      case TokenType::Value:
      case TokenType::EqualSign:
        return TransitionTo<ParseOptionsState>(context_);
      default:
        oxygen::Unreachable();
      }
    }
    if (default_command_) {
      context_->active_command = default_command_;
      DCHECK_F(context_->positional_tokens.empty());
      std::copy(std::begin(path_segments_), std::end(path_segments_),
        std::back_inserter(context_->positional_tokens));
      return TransitionTo<ParseOptionsState>(context_);
    }
    return ReportError(UnrecognizedCommand(path_segments_));
  }

  auto Handle(const TokenEvent<TokenType::Value>& event)
    -> OneOf<DoNothing, TransitionTo<ParseOptionsState>, ReportError>
  {
    // Protect against calling `Handle` without a prior call to `Enter`
    DCHECK_F(!path_segments_.empty());

    path_segments_.push_back(event.token);
    auto segments_count = path_segments_.size();
    filtered_commands_.erase(
      std::remove_if(filtered_commands_.begin(), filtered_commands_.end(),
        [this, segments_count, &event](const CommandPtr& command) {
          const auto& command_path = command->Path();
          if (command_path.size() < segments_count
            || command_path[segments_count - 1] != event.token) {
            return true;
          }
          if (command->Path().size() == segments_count) {
            last_matched_command_ = command;
          }
          return false;
        }),
      filtered_commands_.end());
    if (filtered_commands_.empty()) {
      if (!last_matched_command_) {
        if (!default_command_) {
          return ReportError(UnrecognizedCommand(path_segments_));
        }
        context_->active_command = default_command_;
        DCHECK_F(context_->positional_tokens.empty());
        // Remove the last pushed token in the path segments as it will be
        // transmitted to the ParseOptionsState as the next event.
        path_segments_.pop_back();
        std::copy(std::begin(path_segments_), std::end(path_segments_),
          std::back_inserter(context_->positional_tokens));
        return TransitionTo<ParseOptionsState>(context_);
      }
      context_->active_command = last_matched_command_;
      return TransitionTo<ParseOptionsState>(context_);
    }
    return DoNothing {};
  }

private:
  [[nodiscard]] auto Commands() const -> const CommandsList&
  {
    return context_->commands;
  }

  void Reset()
  {
    filtered_commands_.clear();
    last_matched_command_.reset();
    default_command_.reset();
    path_segments_.clear();
  }

  CommandsList filtered_commands_;
  CommandPtr last_matched_command_;
  CommandPtr default_command_;
  std::vector<std::string> path_segments_;

  ParserContextPtr context_;
};

/*!
 * \brief The parser's state while parsing command options.
 *
 * This state is entered from the InitialState, IdentifyCommandState or
 * ParseShortOptionState with any token except a TokenType::EndOfInput token.
 *
 * **Parser context**:
 *
 * - Upon entering from the ParseShortOptionState state, it is expected that the
 *   existing parser context is valid. Otherwise, the parser context must be
 *   passed as data to the action transitioning into this state. It is expected
 *   that such context contains a valid `active_command`. Optionally, it may
 *   also contain values in the `positional_tokens` list.
 * - This state will add any token of type TokenType::Value it encounters to the
 *   `positional_tokens` list.
 *
 * **Transitions**:
 *
 * - ParseOptionsState: if the current token is a `TokenType::Value`, it will be
 *   added to the `positional_tokens` list and the state machine will stay at
 *   the same state.
 * - ParseShortOptionState: if the current token is a `TokenType::ShortOption`,
 *   `TokenType::LongOption` or `TokenType::LoneDash`.
 * - DashDashState: if the current token is a `TokenType::DashDash`.
 * - FinalState: if the current token is a `TokenType::EndOfInput`.
 *
 * **Errors**:
 *
 * None.
 */
struct ParseOptionsState {

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context_->active_command);
    // recycle the event that transitioned us here so that we dispatch it
    // properly to the next state.
    return ReissueEvent {};
  }

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& /*event*/) -> Status
  {
    // Entering from a ParseShortOptionState, which means that must already have
    // a valid parser context.
    DCHECK_NOTNULL_F(context_);
    // recycle the event that transitioned us here so that we dispatch it
    // properly to the right event handler.
    return ReissueEvent {};
  }

  auto Handle(const TokenEvent<TokenType::Value>& event) -> DoNothing
  {
    // This may be a positional argument. Store it for later processing with the
    // rest of positional arguments.
    context_->positional_tokens.push_back(event.token);
    return DoNothing {};
  }

  auto Handle(const TokenEvent<TokenType::EndOfInput>& /*event*/)
    -> TransitionTo<FinalState>
  {
    return TransitionTo<FinalState> { context_ };
  }

  auto Handle(const TokenEvent<TokenType::DashDash>& /*event*/)
    -> TransitionTo<DashDashState>
  {
    return TransitionTo<DashDashState> { context_ };
  }

  auto Handle(const TokenEvent<TokenType::LongOption>& /*event*/)
    -> TransitionTo<ParseLongOptionState>
  {
    return TransitionTo<ParseLongOptionState> { context_ };
  }

  auto Handle(const TokenEvent<TokenType::EqualSign>& /*event*/) -> ReportError
  {
    return ReportError(OptionSyntaxError(context_));
  }

  template <TokenType token_type>
  auto Handle(const TokenEvent<token_type>& /*event*/)
    -> TransitionTo<ParseShortOptionState>
  {
    static_assert(token_type == TokenType::ShortOption
      || token_type == TokenType::LoneDash);
    return TransitionTo<ParseShortOptionState> { context_ };
  }

private:
  ParserContextPtr context_;

  friend struct ParseOptionsStateTestData;
};

inline auto TryImplicitValue(const ParserContextPtr& context) -> bool
{
  const auto semantics = context->active_option->value_semantic();
  std::any value;
  std::string value_as_text;
  if (semantics->ApplyImplicit(value, value_as_text)) {
    context->ovm.StoreValue(
      context->active_option->Key(), { value, value_as_text, false });
    return true;
  }
  return false;
}

[[nodiscard]] inline auto CheckMultipleOccurrence(
  const ParserContextPtr& context) -> bool
{
  const auto semantics = context->active_option->value_semantic();
  const auto occurrences
    = context->ovm.OccurrencesOf(context->active_option->Key());
  return (occurrences < 1 || semantics->IsRepeatable());
}

/*!
 * \brief The parser's state while parsing a command option present on the
 * command line with its short name.
 *
 * This state is entered only from the ParseOptionsState with a
 * `TokenType::ShortOption` or `TokenType::LoneDash` token.
 *
 * **Parser context**:
 *
 * - Upon entering, it is expected that a `ParserContext` object is passed as
 *   `data` to the `OnEnter()` handler. It is expected that such context
 *   contains a valid `active_command`.
 * - This state will update `active_option` and `active_option_flag` fields with
 *   the option currently being parsed. Both flags are primarily used within
 *   this state but they are also used for diagnostics messages outside.
 * - If the option takes a value and the last parsed token is a
 *   `TokenType::Value`, this state will  store the option value in the context.
 *
 * **Transitions**:
 *
 * - ParseOptionsState: immediately after taking one more token. The token is
 *   eventually processed as a value for the option (if the option can take more
 *   values).
 *
 * **Errors**:
 *
 * - UnrecognizedOption: if the first token provided when entering this state
 *   does not match any of the options defined for the `active_command`.
 * - InvalidValueForOption: if the value token failed to parse as a valid value
 *   for the option and the option does not have an implicit value.
 * - MissingValueForOption: if the current token is not a `TokenType::Value` and
 *   the option does not have an implicit value.
 */
struct ParseShortOptionState
  : Will<ByDefault<TransitionTo<ParseOptionsState>>> {
  using Will::Handle;

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& event, std::any data) -> Status
  {
    static_assert(token_type == TokenType::ShortOption
      || token_type == TokenType::LoneDash);
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context_->active_command);

    std::optional<OptionPtr> option;
    switch (token_type) {
    case TokenType::ShortOption:
      [[fallthrough]];
    case TokenType::LoneDash:
      context_->active_option_flag = "-" + event.token;
      option = context_->active_command->FindShortOption(event.token);
      break;
    case TokenType::LongOption:
    case TokenType::DashDash:
    case TokenType::Value:
    case TokenType::EqualSign:
    case TokenType::EndOfInput:
    default:
      // See contract assertions for entering this state
      oxygen::Unreachable();
    }
    if (!option) {
      return TerminateWithError { UnrecognizedOption(context_, event.token) };
    }
    context_->active_option.swap(option.value());
    if (!CheckMultipleOccurrence(context_)) {
      return TerminateWithError { IllegalMultipleOccurrence(context_) };
    }
    return Continue {};
  }

  template <TokenType token_type>
  auto OnLeave(const TokenEvent<token_type>& /*event*/) -> Status
  {
    if (!value_) {
      if (!TryImplicitValue(context_)) {
        const auto semantics = context_->active_option->value_semantic();
        if (semantics->IsRequired()) {
          return TerminateWithError { MissingValueForOption(context_) };
        }
      }
    }
    Reset();

    return Continue {};
  }

  auto Handle(const TokenEvent<TokenType::Value>& event)
    -> OneOf<DoNothing, ReportError, TransitionTo<ParseOptionsState>>
  {
    DCHECK_NOTNULL_F(context_->active_option);
    const auto semantics = context_->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);

    // If we already accepted a value, we're done
    // TODO(Abdessattar): possibly support multi-token values
    if (value_) {
      return TransitionTo<ParseOptionsState> {};
    }

    // Try the value and if it fails parsing, try the implicit value, if
    // none is available, then fail
    std::any value;
    if (semantics->Parse(value, event.token)) {
      context_->ovm.StoreValue(
        context_->active_option->Key(), { value, event.token, false });
      value_ = event.token;
      return DoNothing {};
    }
    if (TryImplicitValue(context_)) {
      value_ = "_implicit_";
      return TransitionTo<ParseOptionsState> {};
    }
    return ReportError(MissingValueForOption(context_));
  }

private:
  void Reset() { value_.reset(); }

  ParserContextPtr context_;
  std::optional<std::string> value_;

  friend struct ParseShortOptionStateTestData;
};

/*!
 * \brief The parser's state while parsing a command option present on the
 * command line with its long name.
 *
 * This state is entered only from the ParseOptionsState with a
 * `TokenType::LongOption` token.
 *
 * **Parser context**:
 *
 * - Upon entering, it is expected that a `ParserContext` object is passed as
 *   `data` to the `OnEnter()` handler. It is expected that such context
 *   contains a valid `active_command`.
 * - This state will update the context's `action_option` and
 *   `active_option_flag` fields with the option currently being parsed. Both
 *   flags are primarily used within this state but they are also used for
 *   diagnostics messages outside.
 * - If the parsed option takes a value and the last parsed tokens are an
 *   optional `TokenType::EqualSign` followed by a `TokenType::Value`, this
 *   state store the option value in the context.
 *
 * **Transitions**:
 *
 * - ParseLongOptionState: if the current token is a `TokenType::EqualSign`.
 * - ParseOptionsState: if the current token is not a `TokenType::EqualSign`. If
 *   that token is a `TokenType::Value` it will eventually get processed as a
 *   value for the option (if the option can take more values)
 *
 * **Errors**:
 *
 * - UnrecognizedOption: if the first token provided when entering this state
 *   does not match any of the options defined for the `active_command`.
 * - InvalidValueForOption: if the value token failed to parse as a valid value
 *   for the option and the option does not have an implicit value.
 * - MissingValueForOption: if the current token is not a `TokenType::Value` and
 *   the option does not have an implicit value.
 */
struct ParseLongOptionState : Will<ByDefault<TransitionTo<ParseOptionsState>>> {
  using Will::Handle;

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& event, std::any data) -> Status
  {
    static_assert(token_type == TokenType::LongOption);
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context_->active_command);

    std::optional<OptionPtr> option;
    switch (token_type) {
    case TokenType::LongOption:
      context_->active_option_flag = "--" + event.token;
      option = context_->active_command->FindLongOption(event.token);
      break;
    case TokenType::ShortOption:
    case TokenType::LoneDash:
    case TokenType::DashDash:
    case TokenType::Value:
    case TokenType::EqualSign:
    case TokenType::EndOfInput:
    default:
      // See contract assertions for entering this state
      oxygen::Unreachable();
    }
    if (!option) {
      return TerminateWithError { UnrecognizedOption(context_, event.token) };
    }
    context_->active_option.swap(option.value());
    if (!CheckMultipleOccurrence(context_)) {
      return TerminateWithError { IllegalMultipleOccurrence(context_) };
    }
    return Continue {};
  }

  template <TokenType token_type>
  auto OnLeave(const TokenEvent<token_type>& /*event*/) -> Status
  {
    if (!value_) {
      if (!TryImplicitValue(context_)) {
        const auto semantics = context_->active_option->value_semantic();
        if (semantics->IsRequired()) {
          return TerminateWithError { MissingValueForOption(context_) };
        }
      }
    }
    Reset();

    return Continue {};
  }

  auto Handle(const TokenEvent<TokenType::EqualSign>& /*event*/)
    -> OneOf<DoNothing, ReportError>
  {
    DCHECK_NOTNULL_F(context_->active_option);
    const auto semantics = context_->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);
    after_equal_sign = true;
    return DoNothing {};
  }

  auto Handle(const TokenEvent<TokenType::Value>& event)
    -> OneOf<DoNothing, TransitionTo<ParseOptionsState>, ReportError>
  {
    DCHECK_NOTNULL_F(context_->active_option);
    const auto semantics = context_->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);

    if (value_) {
      return TransitionTo<ParseOptionsState> {};
    }
    if (!after_equal_sign) {
      if (!context_->allow_long_option_value_with_no_equal) {
        return ReportError(OptionSyntaxError(context_,
          "option name must be followed by '=' sign because this option "
          "takes a value and does not have an implicit one"));
      }
    }

    // Try the value and if it fails parsing, try the implicit value, if
    // none is available, then fail
    std::any value;
    if (semantics->Parse(value, event.token)) {
      context_->ovm.StoreValue(
        context_->active_option->Key(), { value, event.token, false });
      value_ = event.token;
      return DoNothing {};
    }
    if (!after_equal_sign) {
      if (TryImplicitValue(context_)) {
        value_ = "_implicit_";
        return TransitionTo<ParseOptionsState> {};
      }
    }
    return ReportError(MissingValueForOption(context_));
  }

private:
  void Reset()
  {
    after_equal_sign = false;
    value_.reset();
  }

  ParserContextPtr context_;
  std::optional<std::string> value_;
  bool after_equal_sign { false };

  friend struct ParseLongOptionStateTestData;
};

struct DashDashState : Will<ByDefault<DoNothing>> {
  using Will::Handle;

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    return Continue {};
  }

private:
  ParserContextPtr context_;
};

struct FinalState : Will<ByDefault<DoNothing>> {
  using Will::Handle;

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);

    // process buffered positional arguments
    bool before_rest { true };
    auto& positional_args = context_->positional_tokens;
    OptionPtr rest_option {};
    for (const auto& option : context_->active_command->PositionalArguments()) {
      DCHECK_F(option->IsPositional());
      if (!option->IsPositionalRest()) {
        if (before_rest) {
          // Pick a value from positional arguments starting from the front
          StorePositional(option, positional_args.front());
          positional_args.erase(positional_args.begin());
        } else {
          // Pick a value from positional arguments starting from the front
          StorePositional(option, positional_args.back());
          positional_args.pop_back();
        }
      } else {
        rest_option = option;
        before_rest = false;
      }
    }
    if (!positional_args.empty()) {
      if (rest_option) {
        // Put the rest in 'rest'
        for (const auto& token : positional_args) {
          StorePositional(rest_option, token);
        }
        positional_args.clear();
      } else {
        return TerminateWithError { UnexpectedPositionalArguments(context_) };
      }
    }

    // Validate options
    try {
      CheckRequiredOptions(context_->active_command->CommandOptions());
      CheckRequiredOptions(context_->active_command->PositionalArguments());
    } catch (std::exception& error) {
      return TerminateWithError { error.what() };
    }

    // FIXME(Abdessattar) implement notifiers and store_to

    return Terminate {};
  }

private:
  void CheckRequiredOptions(const std::vector<Option::Ptr>& options)
  {
    // Check if we have any required options with default values that were not
    // provided on the command line and use the defaults
    for (const auto& option : options) {
      const auto semantics = option->value_semantic();
      if (!context_->ovm.HasOption(option->Key())) {
        std::any value;
        std::string value_as_text;
        if (!semantics->ApplyDefault(value, value_as_text)) {
          if (option->IsRequired()) {
            throw std::logic_error(
              MissingRequiredOption(context_->active_command, option));
          }
        } else {
          context_->ovm.StoreValue(
            option->Key(), { value, value_as_text, false });
        }
      }
    }
  }
  void StorePositional(const OptionPtr& option, std::string token)
  {
    const auto semantics = option->value_semantic();
    DCHECK_NOTNULL_F(semantics);
    std::any value;
    if (semantics->Parse(value, token)) {
      context_->ovm.StoreValue(
        option->Key(), { value, std::move(token), true });
    }
  }

  ParserContextPtr context_;
};

} // namespace asap::clap::parser::detail
