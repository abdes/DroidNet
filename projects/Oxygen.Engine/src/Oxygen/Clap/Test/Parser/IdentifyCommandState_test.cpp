//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "TestHelpers.h"

using testing::Eq;

namespace oxygen::clap::parser::detail {

namespace {

  class IdentifyCommandStateTest : public StateTest {
  protected:
    auto SetUp() -> void override
    {
      StateTest::SetUp();
      state_ = std::make_unique<IdentifyCommandState>();
    }

    auto EnterState(const Token& token, const ParserContextPtr& context) const
      -> void
    {
      const auto& [token_type, token_value] = token;
      EXPECT_THAT(token_type, Eq(TokenType::kValue));
      const auto first_event = TokenEvent<TokenType::kValue>(token_value);
      state_->OnEnter(first_event, context);
    }

    auto LeaveState() const -> void override
    {
      const auto last_event = TokenEvent<TokenType::kEndOfInput>("");
      state_->OnLeave(last_event);
    }

    [[nodiscard]] auto state() const
      -> const std::unique_ptr<IdentifyCommandState>&
    {
      return state_;
    }

    auto DoCheckStateAfterLastToken(const TestValueType& test_value) -> void
    {
      const auto& [command_paths, args, action_check, state_check] = test_value;

      const Tokenizer tokenizer(args);
      const auto commands = BuildCommands(command_paths);
      OptionValuesMap ovm;
      Command::Ptr command;
      const CommandLineContext base_context("test", command, ovm, 80U);
      const auto context = StateTest::MakeParserContext(base_context, commands);
      EnterState(tokenizer.NextToken(), context);
      while (true) {
        auto token = tokenizer.NextToken();
        if (!ProcessToken(token, state(), action_check, state_check)) {
          break;
        }
      }
    }

  private:
    std::unique_ptr<IdentifyCommandState> state_;
  };

  class IdentifyCommandStateTransitionsTest
    : public IdentifyCommandStateTest,
      public testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(WellFormedScenarios,
    IdentifyCommandStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"just"},
            {"just"},
            FinalStateTransitionTestData{"just"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just"},
            {"just", "hello"},
            ParseOptionsTransitionTestData{"just", {}},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just"},
            {"just", "--hi"},
            ParseOptionsTransitionTestData{"just", {}},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just"},
            {"just", "--"},
            DashDashTransitionTestData{"just"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just", "just do"},
            {"just", "do"},
            FinalStateTransitionTestData{"just do"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just do", "just do it"},
            {"just", "do"},
            FinalStateTransitionTestData{"just do"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just", "just do it"},
            {"just", "do", "it"},
            FinalStateTransitionTestData{"just do it"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just", "just do it", "just do nothing"},
            {"just", "do", "it"},
            FinalStateTransitionTestData{"just do it"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"justice", "just"},
            {"just"},
            FinalStateTransitionTestData{"just"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"default", "just", "just do it", "just it"},
            {"just", "it"},
            FinalStateTransitionTestData{"just it"},
            IdentifyCommandStateTestData{}
        }
      // clang-format on
      ));

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(WellFormedScenariosWithOptions,
    IdentifyCommandStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType {
            {"just", "just do it"},
            {"just", "-f", "--test"},
            ParseOptionsTransitionTestData{"just", {}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"just", "just do it"},
            {"just", "do", "it", "-v"},
            ParseOptionsTransitionTestData{"just do it", {}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"just", "just do it"},
            {"just", "--", "it", "-v"},
            DashDashTransitionTestData{"just"},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"just do", "just do it"},
            {"just", "do", "something"},
            ParseOptionsTransitionTestData{"just do", {}},
            IdentifyCommandStateTestData{}
        } // clang-format on
      ));

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(BacktrackScenarios,
    IdentifyCommandStateTransitionsTest,
    // clang-format off
    ::testing::Values(
        TestValueType {
            {"default", "just do it"},
            {"just", "do", "--test"},
            ParseOptionsTransitionTestData{"default", {"just", "do"}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"default", "just do it"},
            {"just", "do", "--"},
            ParseOptionsTransitionTestData{"default", {"just", "do"}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"default", "just do it"},
            {"just", "do"},
            ParseOptionsTransitionTestData{"default", {"just", "do"}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"default", "just do it"},
            {"just"},
            ParseOptionsTransitionTestData{"default", {"just"}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"default", "just do it"},
            {"just", "--", "something"},
            ParseOptionsTransitionTestData{"default", {"just"}},
            IdentifyCommandStateTestData{}
        },
        TestValueType {
            {"default", "just do it"},
            {"just", "do", "something"},
            ParseOptionsTransitionTestData{"default", {"just", "do"}},
            IdentifyCommandStateTestData{}
        } // clang-format on
      ));

  class IdentifyCommandStateErrorsTest
    : public IdentifyCommandStateTest,
      public testing::WithParamInterface<TestValueType> { };

  // NOLINTNEXTLINE
  INSTANTIATE_TEST_SUITE_P(IllFormedScenarios, IdentifyCommandStateErrorsTest,
    // clang-format off
    ::testing::Values(
        TestValueType{
            {"just do", "just do it"},
            {"just", "not"},
            ReportErrorTransitionTestData{"Unrecognized command"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just do it"},
            {"just", "do", "-f"},
            ReportErrorTransitionTestData{"Unrecognized command"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"do it", "just do it", "just do nothing"},
            {"just", "do"},
            ReportErrorTransitionTestData{"Unrecognized command"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"justice", "just do it"},
            {"just", "-"},
            ReportErrorTransitionTestData{"Unrecognized command"},
            IdentifyCommandStateTestData{}
        },
        TestValueType{
            {"just do it", "just it", "do it"},
            {"just", "do", "--it"},
            ReportErrorTransitionTestData{"Unrecognized command"},
            IdentifyCommandStateTestData{}
        } // clang-format on
      ));

  // NOLINTNEXTLINE
  TEST_P(IdentifyCommandStateTransitionsTest, CheckStateAfterLastToken)
  {
    const auto test_value = GetParam();
    DoCheckStateAfterLastToken(test_value);
  }

  // NOLINTNEXTLINE
  TEST_P(IdentifyCommandStateErrorsTest, CheckStateAfterLastToken)
  {
    const auto test_value = GetParam();
    DoCheckStateAfterLastToken(test_value);
  }

  // NOLINTNEXTLINE
  TEST_F(IdentifyCommandStateTest, OnLeaveResetsTheState)
  {
    const auto test_value
      = TestValueType { { "default", "just do it" }, { "just", "do" },
          ParseOptionsTransitionTestData { "default", { "just", "do" } },
          IdentifyCommandStateTestData {} };
    DoCheckStateAfterLastToken(test_value);
    DoCheckStateAfterLastToken(test_value);
  }
} // namespace

} // namespace oxygen::clap::parser::detail
