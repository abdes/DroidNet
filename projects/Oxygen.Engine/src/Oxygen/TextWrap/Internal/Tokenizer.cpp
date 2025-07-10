//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <utility>

#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/TextWrap/Internal/Tokenizer.h>

using oxygen::fsm::ByDefault;
using oxygen::fsm::Continue;
using oxygen::fsm::DoNothing;
using oxygen::fsm::On;
using oxygen::fsm::ReissueEvent;
using oxygen::fsm::StateMachine;
using oxygen::fsm::Status;
using oxygen::fsm::Terminate;
using oxygen::fsm::TerminateWithError;
using oxygen::fsm::TransitionTo;
using oxygen::fsm::Will;

using oxygen::wrap::internal::Token;
using oxygen::wrap::internal::TokenConsumer;
using oxygen::wrap::internal::TokenType;

// -----------------------------------------------------------------------------
//  TokenType formatting helpers
// -----------------------------------------------------------------------------

auto oxygen::wrap::internal::to_string(const TokenType value) -> const char*
{
  switch (value) {
    // clang-format off
    case TokenType::kChunk: return "Chunk";
    case TokenType::kWhiteSpace: return "WhiteSpace";
    case TokenType::kNewLine: return "NewLine";
    case TokenType::kParagraphMark: return "ParagraphMark";
    case TokenType::kEndOfInput: return "EndOfInput";
    // clang-format on
  }

  return "__Unknown__";
}

auto oxygen::wrap::internal::operator<<(
  std::ostream& out, const TokenType& token_type) -> std::ostream&
{
  out << to_string(token_type);
  return out;
}

// -----------------------------------------------------------------------------
//  Tokenizer state machine events
// -----------------------------------------------------------------------------

namespace {

struct NonWhiteSpaceChar {
  explicit NonWhiteSpaceChar(const char character)
    : value { character }
  {
  }
  char value;
};
struct WhiteSpaceChar {
  explicit WhiteSpaceChar(const char character)
    : value { character }
  {
  }
  char value;
};
struct InputEnd { };

} // namespace

// -----------------------------------------------------------------------------
//  Tokenizer state machine states
// -----------------------------------------------------------------------------

namespace {

struct InitialState;
struct WordState;
struct WhiteSpaceState;
struct FinalState;

/*!
 * \brief Pass a token to a token consumer.
 *
 * This utility function passes the token to the token consumer and clears the
 * token before returning.
 */
auto DispatchTokenToConsumer(const TokenConsumer& consume_token,
  const TokenType token_type, std::string& token) -> void
{
  consume_token(token_type, token);
  token.clear();
}

/*!
 * \brief The final state of the tokenizer state machine.
 *
 * When the state machine enters this state, a final token event of type
 * TokenType::EndOfInput will be dispatched to the token consumer. After that,
 * the state machine is a completed execution state and no more tokens will be
 * produced. The tokenizer can be reused for a new input text.
 */
struct FinalState : Will<ByDefault<DoNothing>> {
  explicit FinalState(TokenConsumer callback)
    : consume_token { std::move(callback) }
  {
  }

  [[maybe_unused]] [[nodiscard]] auto OnEnter(const InputEnd& /*event*/) const
    -> Status
  {
    consume_token(TokenType::kEndOfInput, "");
    return Terminate {};
  }

private:
  TokenConsumer consume_token;
};

/*!
 * \brief The initial state of the tokenizer state machine.
 *
 * This is the starting state of the tokenizer. Based on which first token will
 * be extracted from the input text, this state will transition to one of the
 * next states.
 */
struct InitialState : Will<On<NonWhiteSpaceChar, TransitionTo<WordState>>,
                        On<WhiteSpaceChar, TransitionTo<WhiteSpaceState>>,
                        On<InputEnd, TransitionTo<FinalState>>> {
  using Will::Handle;
};

/*!
 * \brief Tokenizer state while forming a chunk of non-white-space text.
 *
 * In this state, the tokenizer accumulates input characters into a chunk until
 * it encounters a white space character, a hyphen when break_on_hyphens is
 * `true` or the end of input. At that moment, it dispatches the chunk as a
 * token to the token consumer and transitions to the next state corresponding
 * to the last event.
 */
struct WordState : Will<On<InputEnd, TransitionTo<FinalState>>,
                     On<WhiteSpaceChar, TransitionTo<WhiteSpaceState>>> {
  using Will::Handle;

  WordState(TokenConsumer callback, const bool _break_on_hyphens)
    : consume_token { std::move(callback) }
    , break_on_hyphens { _break_on_hyphens }
  {
  }

  [[maybe_unused]] static auto OnEnter(const NonWhiteSpaceChar& /*event*/)
    -> Status
  {
    return ReissueEvent {};
  }

  template <typename Event> auto OnLeave(const Event& /*event*/) -> Status
  {
    if (!token.empty()) {
      DispatchTokenToConsumer(consume_token, TokenType::kChunk, token);
    }
    return Continue {};
  }

  [[maybe_unused]] auto Handle(const NonWhiteSpaceChar& event) -> DoNothing
  {
    if (break_on_hyphens && event.value == '-' && !token.empty()
      && (std::isalpha(token.back()) != 0)) {
      token.push_back(event.value);
      DispatchTokenToConsumer(consume_token, TokenType::kChunk, token);
    } else {
      token.push_back(event.value);
    }
    return DoNothing {};
  }

private:
  std::string token;
  TokenConsumer consume_token;
  bool break_on_hyphens;
};

/*!
 * \brief Tokenizer state while getting white-space as input characters.
 *
 * In this state, the tokenizer is waiting for a non-white-space to start
 * forming a new chunk or for the end of input to terminate. Any white space
 * characters are collected into a temporary token to be transformed as per the
 * configured parameters (see Tokenizer class description for a detailed
 * description of these parameters). The white space token is then dispatched to
 * the token consumer and the state machine transitions into the next state
 * based on the last event.
 */
struct WhiteSpaceState : Will<On<InputEnd, TransitionTo<FinalState>>,
                           On<NonWhiteSpaceChar, TransitionTo<WordState>>> {
  using Will::Handle;

  explicit WhiteSpaceState(TokenConsumer callback, const bool _collapse_ws)
    : consume_token { std::move(callback) }
    , collapse_ws { _collapse_ws }
  {
  }

  [[maybe_unused]] static auto OnEnter(const WhiteSpaceChar& /*event*/)
    -> Status
  {
    return ReissueEvent {};
  }

  template <typename Event> auto OnLeave(const Event& /*event*/) -> Status
  {
    if (!token.empty()) {
      // This is not a paragraph mark so dispatch as white space or new line
      // token based on the last seen character
      if (last_was_newline) {
        token.pop_back();
        if (!token.empty()) {
          DispatchToConsumer(TokenType::kWhiteSpace);
        }
        DispatchToConsumer(TokenType::kNewLine);
      } else {
        DispatchToConsumer(TokenType::kWhiteSpace);
      }
    }
    last_was_newline = false;
    return Continue {};
  }

  [[maybe_unused]] auto Handle(const WhiteSpaceChar& event) -> DoNothing
  {
    if (event.value == '\n' || event.value == '\v') {
      if (last_was_newline) {
        token.pop_back();
        if (!token.empty()) {
          DispatchToConsumer(TokenType::kWhiteSpace);
        }
        DispatchToConsumer(TokenType::kParagraphMark);
        token = "";
        last_was_newline = false;
        return DoNothing {};
      }
      last_was_newline = true;
      token.push_back('\n');
    } else {
      if (last_was_newline) {
        last_was_newline = false;
        token.pop_back();
        if (!token.empty()) {
          DispatchToConsumer(TokenType::kWhiteSpace);
        }
        DispatchToConsumer(TokenType::kNewLine);
      }
      token.push_back(event.value);
    }
    return DoNothing {};
  }

private:
  auto DispatchToConsumer(const TokenType token_type) -> void
  {
    // If the token is a white space, and we need to collapse white spaces, do
    // it now.
    if (token_type == TokenType::kWhiteSpace) {
      if (collapse_ws) {
        token = " ";
      }
      DispatchTokenToConsumer(consume_token, TokenType::kWhiteSpace, token);
    } else {
      DispatchTokenToConsumer(consume_token, token_type, token);
    }
  }

  bool last_was_newline { false };
  std::string token;
  TokenConsumer consume_token;
  bool collapse_ws;
};

} // namespace

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

auto oxygen::wrap::internal::Tokenizer::Tokenize(
  const std::string& text, const TokenConsumer& consume_token) const -> bool
{

  using Machine
    = StateMachine<InitialState, WordState, WhiteSpaceState, FinalState>;
  Machine machine { InitialState(), WordState(consume_token, break_on_hyphens_),
    WhiteSpaceState(consume_token, collapse_ws_), FinalState(consume_token) };

  bool continue_running { true };
  bool no_errors { true };
  bool reissue = false;

  auto cursor = text.begin();
  while (cursor != text.end()) {
    // '\r' amd '\f' are mot helpful or useful in proper formatting of the
    // wrapped text. They are simply ignored.
    if (*cursor == '\r' || *cursor == '\f') {
      ++cursor;
      continue;
    }

    Status execution_status;

    std::string transformed { (*cursor) };
    // Expand tabs
    if (*cursor == '\t') {
      transformed = tab_;
    }

    auto transformed_cursor = transformed.begin();
    while (transformed_cursor != transformed.end()) {

      if (std::isspace(*transformed_cursor) != 0) {
        execution_status
          = machine.Handle(WhiteSpaceChar { *transformed_cursor });
      } else {
        execution_status
          = machine.Handle(NonWhiteSpaceChar { *transformed_cursor });
      }
      // https://en.cppreference.com/w/cpp/utility/variant/visit
      std::visit(Overload {
                   [&continue_running](const Continue& /*status*/) noexcept {
                     continue_running = true;
                   },
                   [&continue_running](const Terminate& /*status*/) noexcept {
                     continue_running = false;
                   },
                   [&continue_running, &no_errors](
                     const TerminateWithError& /*status*/) noexcept {
                     continue_running = false;
                     no_errors = false;
                   },
                   [&continue_running, &reissue](
                     const ReissueEvent& /*status*/) noexcept {
                     reissue = true;
                     continue_running = true;
                   },
                 },
        execution_status);
      if (continue_running) {
        if (reissue) {
          reissue = false;
          // reuse the same token again
        } else {
          ++transformed_cursor;
        }
      }
    }
    if (continue_running) {
      if (reissue) {
        reissue = false;
        // reuse the same token again
      } else {
        ++cursor;
      }
    }
  }
  machine.Handle(InputEnd {});

  return no_errors;
}
