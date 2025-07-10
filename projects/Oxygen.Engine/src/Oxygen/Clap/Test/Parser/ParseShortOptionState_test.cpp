// ===----------------------------------------------------------------------===/
//  Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
//  copy at https://opensource.org/licenses/BSD-3-Clause).
//  SPDX-License-Identifier: BSD-3-Clause
// ===----------------------------------------------------------------------===/

#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Clap/Fluent/DSL.h>

#include "TestHelpers.h"

using testing::IsTrue;

namespace asap::clap::parser::detail {

namespace {

  class ParseShortOptionStateTest : public StateTest {
  public:
    [[maybe_unused]] static void SetUpTestSuite()
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
    void SetUp() override
    {
      StateTest::SetUp();
      state_ = std::make_unique<ParseShortOptionState>();
    }

    [[nodiscard]] auto EnterState(
      const Token& token, const ParserContextPtr& context) const -> fsm::Status
    {
      EXPECT_NE(context->active_command, nullptr);

      const auto& [token_type, token_value] = token;
      EXPECT_THAT((token_type == TokenType::ShortOption
                    || token_type == TokenType::LoneDash),
        IsTrue());

      if (token_type == TokenType::ShortOption) {
        const auto first_event
          = TokenEvent<TokenType::ShortOption>(token_value);
        return state_->OnEnter(first_event, context);
      }
      if (token_type == TokenType::LoneDash) {
        const auto first_event = TokenEvent<TokenType::LoneDash>(token_value);
        return state_->OnEnter(first_event, context);
      }
      return fsm::TerminateWithError { "Illegal Token" };
    }

    void LeaveState() const override
    {
      const auto last_event = TokenEvent<TokenType::EndOfInput>("");
      state_->OnLeave(last_event);
    }

    auto state() -> std::unique_ptr<ParseShortOptionState>& { return state_; }
    void DoCheckStateAfterLastToken(const TestValueType& test_value)
    {
      const auto& [command_paths, args, action_check, state_check] = test_value;

      const Tokenizer tokenizer(args);
      const auto commands = BuildCommands(command_paths);
      Command::Ptr command;
      const CommandLineContext base_context("test", command, ovm_);
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
      LeaveState();
    }

  private:
    std::unique_ptr<ParseShortOptionState> state_;
    OptionValuesMap ovm_;
  };

  class ParseShortOptionStateTransitionsTest
    : public ParseShortOptionStateTest,
      public ::testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(OptionTakesNoValue,
    ParseShortOptionStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"-n"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"opt_no_val", "-n", 1, {"true"}}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(OptionTakesOptionalValue,
    ParseShortOptionStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"-f"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"first_opt", "-f", 1, {"1"}}
        },
        TestValueType{
            {"with-options"},
            {"-f", "2"},
            ParseOptionsTransitionTestData{},
            ParseShortOptionStateTestData{"first_opt", "-f", 1, {"2"}}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  TEST_P(ParseShortOptionStateTransitionsTest, TransitionWithNoError)
  {
    const auto test_value = GetParam();
    DoCheckStateAfterLastToken(test_value);
  }

  class ParseShortOptionStateUnrecognizedOptionTest
    : public ParseShortOptionStateTest,
      public ::testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(UnrecognizedOptions,
    ParseShortOptionStateUnrecognizedOptionTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"with-options"},
            {"-d"},
            {},
            {}
        },
        TestValueType{
            {"with-options"},
            {"-df", "2"},
            {},
            {}
        },
        TestValueType{
            {"with-options"},
            {"-"},
            {},
            {}
        }
    )); // clang-format on

  // NOLINTNEXTLINE
  TEST_P(ParseShortOptionStateUnrecognizedOptionTest, FailWithAnException)
  {
    auto test_value = GetParam();
    const auto& [command_paths, args, action_check, state_check] = test_value;

    Tokenizer tokenizer(args);
    const auto commands = BuildCommands(command_paths);
    OptionValuesMap ovm;
    Command::Ptr command;
    const CommandLineContext base_context("test", command, ovm);
    auto context = ParserContext::New(base_context, commands);
    context->active_command = predefined_commands().at("with-options");
    auto token = tokenizer.NextToken();
    auto status = EnterState(token, context);
    EXPECT_THAT(
      std::holds_alternative<fsm::TerminateWithError>(status), IsTrue());
  }

} // namespace

} // namespace asap::clap::parser::detail
