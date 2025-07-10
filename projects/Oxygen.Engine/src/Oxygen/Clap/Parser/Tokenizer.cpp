//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <magic_enum/magic_enum.hpp>

#include <Oxygen/Base/StateMachine.h>
#include <Oxygen/Clap/Parser/Tokenizer.h>

using oxygen::fsm::ByDefault;
using oxygen::fsm::Continue;
using oxygen::fsm::DoNothing;
using oxygen::fsm::Maybe;
using oxygen::fsm::On;
using oxygen::fsm::OneOf;
using oxygen::fsm::StateMachine;
using oxygen::fsm::Status;
using oxygen::fsm::TransitionTo;
using oxygen::fsm::Will;

namespace oxygen::clap::parser {

auto operator<<(std::ostream& out, const TokenType& token_type) -> std::ostream&
{
  out << magic_enum::enum_name(token_type);
  return out;
}

namespace {

  struct InputChar {
    explicit InputChar(const char character)
      : value { character }
    {
    }

    [[nodiscard]] auto Value() const -> char { return value; }

  private:
    char value;
  };
  struct InputEnd { };

  using TokenConsumer
    = std::function<void(TokenType token_type, std::string token)>;

  struct InitialState;
  struct ValueState;
  struct OptionState;
  struct ShortOptionState;
  struct LongOptionState;
  struct DashDashState;
  struct FinalState;

  struct FinalState : Will<ByDefault<DoNothing>> {
    [[maybe_unused]] static auto OnEnter(const InputEnd& /*event*/) -> Status
    {
      //    std::cout << "InputEnd -> FinalState" << std::endl;
      return Continue {};
    }
  };

  struct InitialState : ByDefault<TransitionTo<FinalState>> {
    using ByDefault::Handle;

    template <typename Event>
    static auto OnLeave(const Event& /*event*/) -> Status
    {
      //    std::cout << "InitialState -> " << std::endl;
      return Continue {};
    }

    [[maybe_unused]] static auto Handle(const InputChar& event)
      -> OneOf<TransitionTo<ValueState>, TransitionTo<OptionState>>
    {
      switch (event.Value()) {
      case '-':
        return TransitionTo<OptionState> {};
      default:
        return TransitionTo<ValueState> {};
      }
    }
  };

  struct ValueState : On<InputEnd, TransitionTo<FinalState>> {
    using On::Handle;

    explicit ValueState(TokenConsumer callback)
      : consume_token { std::move(callback) }
    {
    }

    [[maybe_unused]] auto OnEnter(const InputChar& event) -> Status
    {
      //    std::cout << "InputChar(" << event.Value() << ") -> ValueState" <<
      //    std::endl;
      token.push_back((event.Value()));
      return Continue {};
    }

    [[maybe_unused]] [[nodiscard]] auto OnLeave(const InputEnd& /*event*/) const
      -> Status
    {
      //    std::cout << "ValueState -> " << std::endl;
      consume_token(TokenType::kValue, token);
      return Continue {};
    }

    [[maybe_unused]] auto Handle(const InputChar& event) -> DoNothing
    {
      token.push_back(event.Value());
      return DoNothing {};
    }

  private:
    std::string token;
    TokenConsumer consume_token;
  };

  struct OptionState {

    explicit OptionState(TokenConsumer callback)
      : consume_token { std::move(callback) }
    {
    }

    [[maybe_unused]] static auto OnEnter(const InputChar& /*event*/) -> Status
    {
      //    std::cout << "InputChar(" << event.Value() << ") -> OptionState" <<
      //    std::endl;
      return Continue {};
    }

    template <typename Event>
    static auto OnLeave(const Event& /*event*/) -> Status
    {
      //    std::cout << "OptionState -> " << std::endl;
      return Continue {};
    }

    [[maybe_unused]] [[nodiscard]] auto Handle(const InputEnd& /*event*/) const
      -> TransitionTo<FinalState>
    {
      consume_token(TokenType::kLoneDash, "-");
      return TransitionTo<FinalState> {};
    }

    [[maybe_unused]] static auto Handle(const InputChar& event)
      -> OneOf<TransitionTo<DashDashState>, TransitionTo<ShortOptionState>>
    {
      switch (event.Value()) {
      case '-':
        return TransitionTo<DashDashState> {};
      default:
        return TransitionTo<ShortOptionState> {};
      }
    }

  private:
    TokenConsumer consume_token;
  };

  struct ShortOptionState : On<InputEnd, TransitionTo<FinalState>> {
    using On::Handle;

    explicit ShortOptionState(TokenConsumer callback)
      : consume_token { std::move(callback) }
    {
    }

    [[maybe_unused]] [[nodiscard]] auto OnEnter(const InputChar& event) const
      -> Status
    {
      //    std::cout << "InputChar(" << event.Value() << ") ->
      //    ShortOptionState"
      //    << std::endl;
      consume_token(TokenType::kShortOption, std::string { event.Value() });
      return Continue {};
    }

    template <typename Event>
    static auto OnLeave(const Event& /*event*/) -> Status
    {
      //    std::cout << "ShortOptionState -> " << std::endl;
      return Continue {};
    }

    [[maybe_unused]] [[nodiscard]] auto Handle(const InputChar& event) const
      -> DoNothing
    {
      consume_token(TokenType::kShortOption, std::string { event.Value() });
      return DoNothing {};
    }

  private:
    TokenConsumer consume_token;
  };

  struct DashDashState : On<InputChar, TransitionTo<LongOptionState>> {
    using On::Handle;

    explicit DashDashState(TokenConsumer callback)
      : consume_token { std::move(callback) }
    {
    }

    [[maybe_unused]] static auto OnEnter(const InputChar& /*event*/) -> Status
    {
      //    std::cout << "InputChar(" << event.Value() << ") -> DashDashState"
      //    << std::endl;
      return Continue {};
    }

    template <typename Event>
    static auto OnLeave(const Event& /*event*/) -> Status
    {
      //    std::cout << "DashDashState -> " << std::endl;
      return Continue {};
    }

    [[maybe_unused]] [[nodiscard]] auto Handle(const InputEnd& /*event*/) const
      -> TransitionTo<FinalState>
    {
      consume_token(TokenType::kDashDash, "--");
      return TransitionTo<FinalState> {};
    }

  private:
    TokenConsumer consume_token;
  };

  struct LongOptionState : On<InputEnd, TransitionTo<FinalState>> {
    using On::Handle;

    explicit LongOptionState(TokenConsumer callback)
      : consume_token { std::move(callback) }
    {
    }

    [[maybe_unused]] auto OnEnter(const InputChar& event) -> Status
    {
      //    std::cout << "InputChar(" << event.Value() << ") -> LongOptionState"
      //    << std::endl;
      token.push_back((event.Value()));
      return Continue {};
    }

    [[maybe_unused]] [[nodiscard]] auto OnLeave(const InputEnd& /*event*/) const
      -> Status
    {
      //    std::cout << "LongOptionState -> " << std::endl;
      consume_token(TokenType::kLongOption, token);
      return Continue {};
    }

    [[maybe_unused]] auto OnLeave(const InputChar& /*event*/) -> Status
    {
      //    std::cout << "LongOptionState -> " << std::endl;
      after_equal_sign = false;
      return Continue {};
    }

    [[maybe_unused]] auto Handle(const InputChar& event)
      -> Maybe<TransitionTo<ValueState>>
    {
      switch (event.Value()) {
      case '=':
        consume_token(TokenType::kLongOption, token);
        consume_token(TokenType::kEqualSign, "=");
        after_equal_sign = true;
        break;
      default:
        if (after_equal_sign) {
          return TransitionTo<ValueState> {};
        }
        token.push_back(event.Value());
      }
      return DoNothing {};
    }

  private:
    std::string token;
    bool after_equal_sign { false };
    TokenConsumer consume_token;
  };

} // namespace

auto Tokenizer::Tokenize(const std::string& arg) const -> void
{

  const TokenConsumer consume_token
    = [this](TokenType token_type, std::string token) -> void {
    //    std::cout << "New token: " << token_type << " / " << token <<
    //    std::endl;
    this->tokens_.emplace_back(token_type, std::move(token));
  };

  StateMachine machine {
    InitialState {},
    ValueState { consume_token },
    OptionState { consume_token },
    ShortOptionState { consume_token },
    DashDashState { consume_token },
    LongOptionState { consume_token },
    FinalState {},
  };

  auto cursor = arg.begin();
  while (cursor != arg.end()) {
    machine.Handle(InputChar { *cursor });
    ++cursor;
  }
  machine.Handle(InputEnd {});
}

} // namespace oxygen::clap::parser
