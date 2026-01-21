// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#pragma once

#include <map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Parser/Events.h>
#include <Oxygen/Clap/Parser/States.h>

namespace oxygen::clap::parser::detail {

struct FinalStateTransitionTestData;
struct ParseOptionsTransitionTestData;
struct ParseShortOptionTransitionTestData;
struct ParseLongOptionTransitionTestData;
struct DashDashTransitionTestData;
struct IdentifyCommandTransitionTestData;
struct ReportErrorTransitionTestData;
struct DoNothingTransitionTestData;

using ExpectedTransitionData
  = std::variant<FinalStateTransitionTestData, ParseOptionsTransitionTestData,
    ParseShortOptionTransitionTestData, ParseLongOptionTransitionTestData,
    DashDashTransitionTestData, IdentifyCommandTransitionTestData,
    DoNothingTransitionTestData, ReportErrorTransitionTestData>;

struct InitialStateTestData;
struct FinalStateTestData;
struct ParseOptionsStateTestData;
struct ParseShortOptionStateTestData;
struct ParseLongOptionStateTestData;
struct DashDashStateTestData;
struct IdentifyCommandStateTestData;

using ExpectedStateData = std::variant<InitialStateTestData, FinalStateTestData,
  ParseOptionsStateTestData, ParseShortOptionStateTestData,
  ParseLongOptionStateTestData, DashDashStateTestData,
  IdentifyCommandStateTestData>;

using TestValueType = std::tuple<
  // command paths
  std::vector<std::string>,
  // arguments to pass to tokenizer
  std::vector<std::string>,
  // transition check data
  ExpectedTransitionData,
  // state check data
  ExpectedStateData>;

class StateTest : public testing::Test {
public:
  static auto EmptyGlobalOptions() -> const std::vector<OptionPtr>&
  {
    static const std::vector<OptionPtr> empty;
    return empty;
  }

  static auto EmptyGlobalOptionGroups()
    -> const std::vector<std::pair<Options::Ptr, bool>>&
  {
    static const std::vector<std::pair<Options::Ptr, bool>> empty;
    return empty;
  }

  static auto MakeParserContext(const CommandLineContext& base,
    const CommandsList& commands) -> ParserContextPtr
  {
    return ParserContext::New(
      base, commands, EmptyGlobalOptions(), EmptyGlobalOptionGroups());
  }

  static auto predefined_commands() -> std::map<std::string, CommandPtr>&
  {
    OXYGEN_DIAGNOSTIC_PUSH
#if defined(ASAP_CLANG_VERSION)
    OXYGEN_PRAGMA(clang diagnostic ignored "-Wexit-time-destructors")
#endif
    static std::map<std::string, CommandPtr> predefined_commands_ {
      { "default", CommandBuilder(Command::DEFAULT) },
      { "just", CommandBuilder("just") },
      { "just do", CommandBuilder("just", "do") },
      { "do it", CommandBuilder("do", "it") },
      { "just it", CommandBuilder("just", "it") },
      { "just do it", CommandBuilder("just", "do", "it") },
      { "just do nothing", CommandBuilder("just", "do", "nothing") },
      { "justice", CommandBuilder("justice") },
      { "partial", CommandBuilder("partial") }
    };
    OXYGEN_DIAGNOSTIC_POP
    return predefined_commands_;
  }

protected:
  static auto BuildCommands(const std::vector<std::string>& command_paths)
    -> CommandsList
  {
    CommandsList commands;
    for (const auto& command_path : command_paths) {
      commands.push_back(predefined_commands().at(command_path));
    }
    return commands;
  }

  virtual auto LeaveState() const -> void { }

  template <typename State>
  [[nodiscard]] auto ProcessToken(const Token& token, State& state,
    const ExpectedTransitionData& action_data,
    const ExpectedStateData& state_data) const -> bool
  {
    const auto& [token_type, token_value] = token;
    switch (token_type) {
    case TokenType::kShortOption: {
      auto action
        = state->Handle(TokenEvent<TokenType::kShortOption> { token_value });
      auto continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kLongOption: {
      auto action
        = state->Handle(TokenEvent<TokenType::kLongOption> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kLoneDash: {
      auto action
        = state->Handle(TokenEvent<TokenType::kLoneDash> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kEqualSign: {
      auto action
        = state->Handle(TokenEvent<TokenType::kEqualSign> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kDashDash: {
      auto action
        = state->Handle(TokenEvent<TokenType::kDashDash> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kValue: {
      auto action
        = state->Handle(TokenEvent<TokenType::kValue> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        this->LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    case TokenType::kEndOfInput: {
      auto action
        = state->Handle(TokenEvent<TokenType::kEndOfInput> { token_value });
      bool continue_after_check_state { true };
      const bool continue_after_check_action
        = CheckAction(action, token_type, action_data);
      if (!continue_after_check_action) {
        LeaveState();
        continue_after_check_state = CheckState(state, state_data);
      }
      return continue_after_check_action && continue_after_check_state;
    }
    default:;
    }
    Unreachable();
  }

  template <typename ActionType>
  [[nodiscard]] static auto CheckAction(const ActionType& action,
    TokenType token_type, const ExpectedTransitionData& data) -> bool
  {
    if (!action.template IsA<DoNothing>()
      || token_type == TokenType::kEndOfInput) {
      std::visit([&action](auto& test_data) { test_data.Check(action); }, data);
      return false; // do not continue processing tokens
    }
    return true; // continue processing tokens
  }

  template <typename State>
  [[nodiscard]] static auto CheckState(
    const State& state, const ExpectedStateData& data) -> bool
  {
    std::visit([&state](auto& test_data) { test_data.Check(state); }, data);
    return true; // continue processing
  }
};

struct FinalStateTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<FinalState>>(),
      ::testing::IsTrue());
    const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
    EXPECT_THAT(action_data->active_command,
      ::testing::Eq(StateTest::predefined_commands().at(command_path)));
  }
  std::string command_path;
};

struct ParseOptionsTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<ParseOptionsState>>(),
      ::testing::IsTrue());
    if (action.Data().has_value()) {
      const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
      EXPECT_THAT(action_data->active_command,
        ::testing::Eq(StateTest::predefined_commands().at(command_path)));
      EXPECT_THAT(
        action_data->positional_tokens, ::testing::Eq(positional_tokens));
    }
  }
  std::string command_path;
  std::vector<std::string> positional_tokens;
};

struct ParseShortOptionTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<ParseShortOptionState>>(),
      ::testing::IsTrue());
    const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
    EXPECT_THAT(action_data->active_command,
      ::testing::Eq(StateTest::predefined_commands().at(command_path)));
  }
  std::string command_path;
};

struct ParseLongOptionTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<ParseLongOptionState>>(),
      ::testing::IsTrue());
    const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
    EXPECT_THAT(action_data->active_command,
      ::testing::Eq(StateTest::predefined_commands().at(command_path)));
  }
  std::string command_path;
};

struct DashDashTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<DashDashState>>(),
      ::testing::IsTrue());
    const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
    EXPECT_THAT(action_data->active_command,
      ::testing::Eq(StateTest::predefined_commands().at(command_path)));
  }
  std::string command_path;
};

struct IdentifyCommandTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::TransitionTo<IdentifyCommandState>>(),
      ::testing::IsTrue());
    const auto& action_data = std::any_cast<ParserContextPtr>(action.Data());
    EXPECT_THAT(action_data->commands, ::testing::Eq(commands));
  }
  CommandsList commands;
};

struct ReportErrorTransitionTestData {
  template <typename ActionType>
  auto Check(const ActionType& action) const -> void
  {
    EXPECT_THAT(action.template IsA<fsm::ReportError>(), ::testing::IsTrue());
    const auto& action_data = std::any_cast<std::string>(action.Data());
    EXPECT_THAT(action_data, ::testing::HasSubstr(error));
  }
  std::string error;
};

struct DoNothingTransitionTestData {
  template <typename ActionType>
  static auto Check(const ActionType& /*action*/) -> void
  {
    // Do nothing
  }
};

struct InitialStateTestData {
  template <typename State> auto Check(const State& /*state*/) const -> void { }
  CommandsList commands;
};

template <>
inline auto InitialStateTestData::Check(
  const std::unique_ptr<InitialState>& state) const -> void
{
  EXPECT_THAT(state->GetContext()->commands, ::testing::Eq(commands));
}

struct FinalStateTestData {
  template <typename State>
  static auto Check(const State& /*state*/) -> void { }
};
struct ParseOptionsStateTestData {
  template <typename State> auto Check(const State& /*state*/) const -> void { }

  std::vector<std::string> value_tokens;
};

template <>
inline auto ParseOptionsStateTestData::Check(
  const std::unique_ptr<ParseOptionsState>& state) const -> void
{
  EXPECT_THAT(state->context->positional_tokens, ::testing::Eq(value_tokens));
}

struct ParseShortOptionStateTestData {
  template <typename State> auto Check(const State& /*state*/) const -> void { }

  std::string active_option;
  std::string active_option_flag;
  size_t values_size { 0 };
  std::optional<const std::string> value;
};

template <>
inline auto ParseShortOptionStateTestData::Check(
  const std::unique_ptr<ParseShortOptionState>& state) const -> void
{
  const auto& option_name = state->context_->active_option->Key();
  EXPECT_THAT(option_name, ::testing::Eq(active_option));
  EXPECT_THAT(
    state->context_->active_option_flag, ::testing::Eq(active_option_flag));
  EXPECT_THAT(state->context_->ovm.OccurrencesOf(option_name),
    ::testing::Eq(values_size));
  if (value.has_value() && values_size > 0) {
    const auto& last_value
      = state->context_->ovm.ValuesOf(option_name).back().OriginalToken();
    EXPECT_THAT(last_value, ::testing::Eq(value));
  }
}

struct ParseLongOptionStateTestData {
  template <typename State>
  static auto Check(const State& /*state*/) -> void { }
};

struct DashDashStateTestData {
  template <typename State>
  static auto Check(const State& /*state*/) -> void { }
};
struct IdentifyCommandStateTestData {
  template <typename State>
  static auto Check(const State& /*state*/) -> void { }
};

} // namespace oxygen::clap::parser::detail
