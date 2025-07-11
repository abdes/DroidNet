//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Internal/Errors.h>
#include <Oxygen/Clap/Parser/Context.h>
#include <Oxygen/Clap/Parser/Events.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>

namespace oxygen::clap::parser::detail {

using fsm::ByDefault;
using fsm::Continue;
using fsm::DoNothing;
using fsm::Maybe;
using fsm::On;
using fsm::OneOf;
using fsm::ReissueEvent;
using fsm::ReportError;
using fsm::StateMachine;
using fsm::Status;
using fsm::Terminate;
using fsm::TerminateWithError;
using fsm::TransitionTo;
using fsm::Will;

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
 *     initial segment of one of the supported commands.
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
 *   not match the initial segment of one of the supported commands and the CLI
 *   does not have a default command.
 * - MissingCommand: if the current token is not a `TokenType::Value` and the
 *   CLI does not have a default command.
 */
struct InitialState {

  explicit InitialState(ParserContextPtr _context)
    : context { std::move(_context) }
  {
    for (const auto& command : context->commands) {
      if (command->IsDefault()) {
        context->active_command = command;
        break;
      }
    }
  }

  auto Handle(const TokenEvent<TokenType::kValue>& event) -> OneOf<ReportError,
    TransitionTo<ParseOptionsState>, TransitionTo<IdentifyCommandState>>
  {
    // We have a token that could be either a command path segment or a value.
    // If we can find at least one command which path starts with the token then
    // we are sure this is the start of a command path. Otherwise, this can only
    // be a value, and we must have a default command.
    if (MaybeCommand(event.token)) {
      return TransitionTo<IdentifyCommandState>(context);
    }
    if (context->active_command) {
      return TransitionTo<ParseOptionsState>(context);
    }
    return ReportError(UnrecognizedCommand({ event.token }));
  }

  auto Handle(const TokenEvent<TokenType::kEndOfInput>& /*event*/)
    -> OneOf<ReportError, TransitionTo<FinalState>>
  {
    if (context->active_command) {
      return TransitionTo<FinalState>(context);
    }
    return ReportError(MissingCommand(context));
  }

  template <TokenType TokenT>
  auto Handle(const TokenEvent<TokenT>& /*event*/)
    -> OneOf<TransitionTo<ParseOptionsState>, TransitionTo<FinalState>,
      TransitionTo<DashDashState>, ReportError>
  {
    static_assert(TokenT != TokenType::kValue);
    // For any token type other than TokenType::Value, we require a default
    // command to be present.
    if (context->active_command) {
      if constexpr (TokenT == TokenType::kDashDash) {
        return TransitionTo<DashDashState>(context);
      } else {
        return TransitionTo<ParseOptionsState>(context);
      }
    }
    return ReportError(MissingCommand(context));
  }

  [[nodiscard]] auto GetContext() const -> const ParserContextPtr&
  {
    return context;
  }

private:
  [[nodiscard]] auto MaybeCommand(const std::string& token) const -> bool
  {
    return std::ranges::any_of(
      context->commands, [&token](const auto& command) {
        return (!command->Path().empty() && command->Path()[0] == token);
      });
  }

  ParserContextPtr context;
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
 *   `data` to the `OnEnter()` handler, and that such context contains a
 * non-empty list of commands.
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
 * - IdentifyCommandState: as long as tokens encountered are TokenType::Value,
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

  auto OnEnter(const TokenEvent<TokenType::kValue>& event, std::any data)
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
      case TokenType::kDashDash:
        return TransitionTo<DashDashState>(context_);
      case TokenType::kEndOfInput:
        return TransitionTo<FinalState>(context_);
      case TokenType::kLongOption:
      case TokenType::kShortOption:
      case TokenType::kLoneDash:
      case TokenType::kValue:
      case TokenType::kEqualSign:
        return TransitionTo<ParseOptionsState>(context_);
      }
    }
    if (default_command_) {
      context_->active_command = default_command_;
      DCHECK_F(context_->positional_tokens.empty());
      std::ranges::copy(
        path_segments_, std::back_inserter(context_->positional_tokens));
      return TransitionTo<ParseOptionsState>(context_);
    }
    return ReportError(UnrecognizedCommand(path_segments_));
  }

  auto Handle(const TokenEvent<TokenType::kValue>& event)
    -> OneOf<DoNothing, TransitionTo<ParseOptionsState>, ReportError>
  {
    // Protect against calling `Handle` without a prior call to `Enter`
    DCHECK_F(!path_segments_.empty());

    path_segments_.push_back(event.token);
    auto segments_count = path_segments_.size();
    std::erase_if(filtered_commands_,
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
      });
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
        std::ranges::copy(
          path_segments_, std::back_inserter(context_->positional_tokens));
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

  auto Reset() -> void
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

  template <TokenType TokenT>
  auto OnEnter(const TokenEvent<TokenT>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context->active_command);
    // recycle the event that transitioned us here so that we dispatch it
    // properly to the next state.
    return ReissueEvent {};
  }

  template <TokenType TokenT>
  auto OnEnter(const TokenEvent<TokenT>& /*event*/) -> Status
  {
    // Entering from a ParseShortOptionState, which means that must already have
    // a valid parser context.
    DCHECK_NOTNULL_F(context);
    // recycle the event that transitioned us here so that we dispatch it
    // properly to the right event handler.
    return ReissueEvent {};
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  auto Handle(const TokenEvent<TokenType::kValue>& event) -> DoNothing
  {
    // This may be a positional argument. Store it for later processing with the
    // rest of positional arguments.
    context->positional_tokens.push_back(event.token);
    return DoNothing {};
  }

  auto Handle(const TokenEvent<TokenType::kEndOfInput>& /*event*/)
    -> TransitionTo<FinalState>
  {
    return TransitionTo<FinalState> { context };
  }

  auto Handle(const TokenEvent<TokenType::kDashDash>& /*event*/)
    -> TransitionTo<DashDashState>
  {
    return TransitionTo<DashDashState> { context };
  }

  auto Handle(const TokenEvent<TokenType::kLongOption>& /*event*/)
    -> TransitionTo<ParseLongOptionState>
  {
    return TransitionTo<ParseLongOptionState> { context };
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  auto Handle(const TokenEvent<TokenType::kEqualSign>& /*event*/) -> ReportError
  {
    return ReportError(OptionSyntaxError(context));
  }

  template <TokenType token_type>
  auto Handle(const TokenEvent<token_type>& /*event*/)
    -> TransitionTo<ParseShortOptionState>
  {
    static_assert(token_type == TokenType::kShortOption
      || token_type == TokenType::kLoneDash);
    return TransitionTo<ParseShortOptionState> { context };
  }

private:
  ParserContextPtr context;

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
 *   this state, but they are also used for diagnostics messages outside.
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
    static_assert(token_type == TokenType::kShortOption
      || token_type == TokenType::kLoneDash);
    DCHECK_F(data.has_value());
    context_ = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context_->active_command);

    std::optional<OptionPtr> option;
    switch (token_type) {
    case TokenType::kShortOption:
      [[fallthrough]];
    case TokenType::kLoneDash:
      context_->active_option_flag = "-" + event.token;
      option = context_->active_command->FindShortOption(event.token);
      break;
    case TokenType::kLongOption:
    case TokenType::kDashDash:
    case TokenType::kValue:
    case TokenType::kEqualSign:
    case TokenType::kEndOfInput:
      // See contract assertions for entering this state
      Unreachable();
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

  auto Handle(const TokenEvent<TokenType::kValue>& event)
    -> OneOf<DoNothing, ReportError, TransitionTo<ParseOptionsState>>
  {
    DCHECK_NOTNULL_F(context_->active_option);
    const auto semantics = context_->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);

    // If we already accepted a value, we're done
    // TODO(abdes): possibly support multi-token values
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
  auto Reset() -> void { value_.reset(); }

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
 *   flags are primarily used within this state, but they are also used for
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
    static_assert(token_type == TokenType::kLongOption);
    DCHECK_F(data.has_value());
    context = std::any_cast<ParserContextPtr>(data);
    DCHECK_NOTNULL_F(context->active_command);

    std::optional<OptionPtr> option;
    switch (token_type) {
    case TokenType::kLongOption:
      context->active_option_flag = "--" + event.token;
      option = context->active_command->FindLongOption(event.token);
      break;
    case TokenType::kShortOption:
    case TokenType::kLoneDash:
    case TokenType::kDashDash:
    case TokenType::kValue:
    case TokenType::kEqualSign:
    case TokenType::kEndOfInput:
      // See contract assertions for entering this state
      Unreachable();
    }
    if (!option) {
      return TerminateWithError { UnrecognizedOption(context, event.token) };
    }
    context->active_option.swap(option.value());
    if (!CheckMultipleOccurrence(context)) {
      return TerminateWithError { IllegalMultipleOccurrence(context) };
    }
    return Continue {};
  }

  template <TokenType token_type>
  auto OnLeave(const TokenEvent<token_type>& /*event*/) -> Status
  {
    if (!value) {
      if (!TryImplicitValue(context)) {
        const auto semantics = context->active_option->value_semantic();
        if (semantics->IsRequired()) {
          return TerminateWithError { MissingValueForOption(context) };
        }
      }
    }
    Reset();

    return Continue {};
  }

  auto Handle(const TokenEvent<TokenType::kEqualSign>& /*event*/)
    -> OneOf<DoNothing, ReportError>
  {
    DCHECK_NOTNULL_F(context->active_option);
    const auto semantics = context->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);
    after_equal_sign = true;
    return DoNothing {};
  }

  // ReSharper disable once CppMemberFunctionMayBeConst
  auto Handle(const TokenEvent<TokenType::kValue>& event)
    -> OneOf<DoNothing, TransitionTo<ParseOptionsState>, ReportError>
  {
    DCHECK_NOTNULL_F(context->active_option);
    const auto semantics = context->active_option->value_semantic();
    DCHECK_NOTNULL_F(semantics);

    if (value) {
      return TransitionTo<ParseOptionsState> {};
    }
    if (!after_equal_sign) {
      if (!context->allow_long_option_value_with_no_equal) {
        return ReportError(OptionSyntaxError(context,
          "option name must be followed by '=' sign because this option "
          "takes a value and does not have an implicit one"));
      }
    }

    // Try the value and if it fails parsing, try the implicit value, if
    // none is available, then fail
    std::any any_value;
    if (semantics->Parse(any_value, event.token)) {
      context->ovm.StoreValue(
        context->active_option->Key(), { any_value, event.token, false });
      value = event.token;
      return DoNothing {};
    }
    if (!after_equal_sign) {
      if (TryImplicitValue(context)) {
        value = "_implicit_";
        return TransitionTo<ParseOptionsState> {};
      }
    }
    return ReportError(MissingValueForOption(context));
  }

private:
  auto Reset() -> void
  {
    after_equal_sign = false;
    value.reset();
  }

  ParserContextPtr context;
  std::optional<std::string> value;
  bool after_equal_sign { false };

  friend struct ParseLongOptionStateTestData;
};

struct DashDashState : Will<ByDefault<DoNothing>> {
  using Will::Handle;

  template <TokenType token_type>
  auto OnEnter(const TokenEvent<token_type>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context = std::any_cast<ParserContextPtr>(data);
    return Continue {};
  }

private:
  ParserContextPtr context;
};

struct FinalState : Will<ByDefault<DoNothing>> {
  using Will::Handle;

  template <TokenType TokenT>
  auto OnEnter(const TokenEvent<TokenT>& /*event*/, std::any data) -> Status
  {
    DCHECK_F(data.has_value());
    context = std::any_cast<ParserContextPtr>(data);

    // process buffered positional arguments
    bool before_rest { true };
    auto& positional_args = context->positional_tokens;
    OptionPtr rest_option {};
    for (const auto& option : context->active_command->PositionalArguments()) {
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
        return TerminateWithError { UnexpectedPositionalArguments(context) };
      }
    }

    // Validate options
    try {
      CheckRequiredOptions(context->active_command->CommandOptions());
      CheckRequiredOptions(context->active_command->PositionalArguments());
    } catch (std::exception& error) {
      return TerminateWithError { error.what() };
    }

    // TODO: implement notifiers and store_to

    return Terminate {};
  }

private:
  auto CheckRequiredOptions(const std::vector<Option::Ptr>& options) const
    -> void
  {
    // Check if we have any required options with default values that were not
    // provided on the command line and use the defaults
    for (const auto& option : options) {
      const auto semantics = option->value_semantic();
      if (!context->ovm.HasOption(option->Key())) {
        std::any value;
        std::string value_as_text;
        if (!semantics->ApplyDefault(value, value_as_text)) {
          if (option->IsRequired()) {
            throw std::logic_error(
              MissingRequiredOption(context->active_command, option));
          }
        } else {
          context->ovm.StoreValue(
            option->Key(), { value, value_as_text, false });
        }
      }
    }
  }

  auto StorePositional(const OptionPtr& option, std::string token) const -> void
  {
    const auto semantics = option->value_semantic();
    DCHECK_NOTNULL_F(semantics);
    std::any value;
    if (semantics->Parse(value, token)) {
      context->ovm.StoreValue(option->Key(), { value, std::move(token), true });
    }
  }

  ParserContextPtr context;
};

} // namespace oxygen::clap::parser::detail
