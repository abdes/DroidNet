// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause.
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#include <variant>

#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Clap/Fluent/DSL.h>

#include "TestHelpers.h"

using testing::Eq;
using testing::IsTrue;

namespace oxygen::clap::parser::detail {

namespace {

  class ParseLongOptionStateTest : public StateTest {
  public:
    [[maybe_unused]] static auto SetUpTestSuite() -> void
    {
      const Command::Ptr my_command { CommandBuilder("with-options")
          .WithOption(Option::WithKey("opt_no_val")
              .About("Option that takes no values")
              .Short("n")
              .Long("no-value")
              .WithValue<bool>()
              .DefaultValue(false, "false")
              .ImplicitValue(true, "true")
              .Build())
          .WithOption(Option::WithKey("first_opt")
              .About("The first option")
              .Short("f")
              .Long("first-option")
              .WithValue<std::string>()
              .DefaultValue("1")
              .ImplicitValue("1")
              .Build())
          .WithOption(Option::WithKey("second_opt")
              .About("The second option")
              .Short("s")
              .Long("second-option")
              .WithValue<std::string>()
              .Repeatable()
              .Build())
          .Build() };
      predefined_commands()["with-options"] = my_command;
    }

  protected:
    auto SetUp() -> void override
    {
      StateTest::SetUp();
      state_ = std::make_unique<ParseLongOptionState>();
    }

    [[nodiscard]] auto EnterState(
      const Token& token, const ParserContextPtr& context) const -> Status
    {
      EXPECT_NE(context->active_command, nullptr);

      const auto& [token_type, token_value] = token;
      EXPECT_THAT(token_type, Eq(TokenType::kLongOption));
      const auto first_event = TokenEvent<TokenType::kLongOption>(token_value);
      return state_->OnEnter(first_event, context);
    }

    auto LeaveState() const -> void override
    {
      const auto last_event = TokenEvent<TokenType::kEndOfInput>("");
      state_->OnLeave(last_event);
    }

    auto state() -> std::unique_ptr<ParseLongOptionState>& { return state_; }
    auto DoCheckStateAfterLastToken(const TestValueType& test_value) -> void
    {
      const auto& [command_paths, args, action_check, state_check] = test_value;

      const Tokenizer tokenizer(args);
      const auto commands = BuildCommands(command_paths);
      Command::Ptr command;
      const CommandLineContext base_context("test", command, ovm_, 80U);
      const auto context = ParserContext::New(base_context, commands);
      context->active_command = predefined_commands().at("with-options");
      auto token = tokenizer.NextToken();
      // NOLINTNEXTLINE(hicpp-avoid-goto, cppcoreguidelines-avoid-goto)
      EXPECT_NO_THROW(auto status = EnterState(token, context));
      while (true) {
        token = tokenizer.NextToken();
        if (!ProcessToken(token, state(), action_check, state_check)) {
          break;
        }
      }
    }

  private:
    std::unique_ptr<ParseLongOptionState> state_;
    OptionValuesMap ovm_;
  };

  class ParseLongOptionStateTransitionsTest
    : public ParseLongOptionStateTest,
      public testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(OptionTakesNoValue,
    ParseLongOptionStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"--no-value", "2"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"opt_no_val", "--no-value", 1, {"true"}}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(OptionTakesOptionalValue,
    ParseLongOptionStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"--first-option"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"first_opt", "--no-value", 1, {"1"}}
        },
        TestValueType{
            {"with-options"},
            {"--first-option", "222"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"first_opt", "--no-value", 1, {"222"}}
        },
        TestValueType{
            {"with-options"},
            {"--first-option=333"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"first_opt", "--no-value", 1, {"333"}}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  TEST_P(ParseLongOptionStateTransitionsTest, TransitionWithNoError)
  {
    const auto test_value = GetParam();
    DoCheckStateAfterLastToken(test_value);
  }

  class ParseLongOptionStateUnrecognizedOptionTest
    : public ParseLongOptionStateTest,
      public testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(UnrecognizedOptions,
    ParseLongOptionStateUnrecognizedOptionTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"--not-option"},
            {},
            {}
        },
        TestValueType{
            {"with-options"},
            {"--second"},
            {},
            {}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  TEST_P(ParseLongOptionStateUnrecognizedOptionTest, FailWithAnException)
  {
    auto test_value = GetParam();
    const auto& [command_paths, args, action_check, state_check] = test_value;

    Tokenizer tokenizer(args);
    const auto commands = BuildCommands(command_paths);
    OptionValuesMap ovm;
    Command::Ptr command;
    const CommandLineContext base_context("test", command, ovm, 80U);
    auto context = ParserContext::New(base_context, commands);
    context->active_command = predefined_commands().at("with-options");
    auto token = tokenizer.NextToken();
    auto status = EnterState(token, context);
    EXPECT_THAT(
      std::holds_alternative<fsm::TerminateWithError>(status), IsTrue());
  }

} // namespace

} // namespace oxygen::clap::parser::detail
