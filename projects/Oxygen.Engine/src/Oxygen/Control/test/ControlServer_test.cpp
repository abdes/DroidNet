//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "TestEngine.h"
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Control/ControlServer.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::control::CommandDispatcher;
using oxygen::control::ControlServer;
using oxygen::control::ControlServerConfig;
using oxygen::control::EventLog;
using oxygen::control::testing::TestEngine;
using tcp = asio::ip::tcp;

//! A line-based JSON client over the test's I/O context.
class Client {
public:
  explicit Client(asio::io_context& io)
    : socket_(io)
  {
  }

  auto Connect(const uint16_t port) -> Co<>
  {
    co_await socket_.async_connect(
      tcp::endpoint(asio::ip::address_v4::loopback(), port),
      oxygen::co::asio_awaitable);
  }

  auto Send(const json& message) -> Co<>
  {
    const auto line = message.dump() + "\n";
    co_await asio::async_write(
      socket_, asio::buffer(line), oxygen::co::asio_awaitable);
  }

  //! The next message, or null when the server closed the connection.
  auto Receive() -> Co<json>
  {
    const auto [error, length] = co_await asio::async_read_until(socket_,
      asio::dynamic_buffer(pending_), '\n', oxygen::co::asio_nothrow_awaitable);
    if (error) {
      co_return json {};
    }
    auto message = json::parse(pending_.substr(0U, length));
    pending_.erase(0U, length);
    co_return message;
  }

  auto Close() -> void
  {
    auto ignored = std::error_code {};
    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
  }

  //! Sends a request and returns its reply, skipping notifications.
  auto Call(const int id, const std::string& method,
    const json& params = json::object()) -> Co<json>
  {
    co_await Send({
      { "jsonrpc", "2.0" },
      { "id", id },
      { "method", method },
      { "params", params },
    });
    for (;;) {
      auto message = co_await Receive();
      if (message.is_null() || message.value("id", -1) == id) {
        co_return message;
      }
    }
  }

private:
  tcp::socket socket_;
  std::string pending_;
};

//! A dispatcher and a server over a real loopback socket; a ticker plays
//! the engine's frame starts.
class ControlServerTest : public testing::Test {
protected:
  ControlServerTest() { dispatcher_.RegisterCommands(engine_.GetCommands()); }

  //! Serves with `config_` while `body` runs.
  template <typename Body> auto RunServing(Body body) -> void
  {
    server_ = std::make_unique<ControlServer>(
      observer_ptr<oxygen::IAsyncEngine> { &engine_ },
      observer_ptr { &dispatcher_ }, observer_ptr { &events_ }, io_, config_);
    oxygen::co::Run(io_, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&CommandDispatcher::ActivateAsync, &dispatcher_);
        co_await n.Start(&ControlServer::ActivateAsync, server_.get());
        server_->Run();
        n.Start([&]() -> Co<> {
          for (uint64_t frame = 1;; ++frame) {
            dispatcher_.OnFrameStart(frame, true);
            auto timer = asio::steady_timer(io_, 1ms);
            co_await timer.async_wait(oxygen::co::asio_awaitable);
          }
        });
        co_await body();
        co_return oxygen::co::kCancel;
      };
    });
  }

  [[nodiscard]] auto Token() const -> std::string
  {
    auto file = std::ifstream(server_->DiscoveryFile());
    const auto text = std::string(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char> {});
    return json::parse(text).at("token").get<std::string>();
  }

  asio::io_context io_;
  TestEngine engine_;
  EventLog events_;
  CommandDispatcher dispatcher_ {
    observer_ptr<oxygen::IAsyncEngine> { &engine_ }, observer_ptr { &events_ }
  };
  ControlServerConfig config_ { .application = "ControlServerTest" };
  std::unique_ptr<ControlServer> server_;
};

//! A client says hello with the token, runs a command, and catches up on the
//! events it produced.
NOLINT_TEST_F(ControlServerTest, RunsCommandsForAnAuthenticatedClient)
{
  RunServing([&]() -> Co<> {
    auto client = Client(io_);
    co_await client.Connect(server_->Port());

    const auto hello = co_await client.Call(
      1, "rpc.hello", { { "token", Token() }, { "client", "test" } });
    const auto pause = co_await client.Call(2, "ngin.pause");
    const auto bad = co_await client.Call(3, "ngin.step", { { "frames", 0 } });
    const auto unknown = co_await client.Call(4, "no.such.command");
    const auto events
      = co_await client.Call(5, "rpc.events", { { "since", 0 } });

    EXPECT_EQ(hello["result"]["application"], "ControlServerTest");
    EXPECT_TRUE(pause.contains("result"));
    EXPECT_TRUE(engine_.paused);
    EXPECT_EQ(bad["error"]["code"], -32602);
    EXPECT_EQ(unknown["error"]["code"], -32601);
    // ASSERT_* cannot return from a coroutine.
    const auto& published = events["result"]["events"];
    EXPECT_FALSE(published.empty());
    if (!published.empty()) {
      EXPECT_EQ(published.back()["topic"], "command");
      EXPECT_EQ(published.back()["data"]["source"], "remote");
    }
  });
}

//! Without the right token the server refuses and closes the connection.
NOLINT_TEST_F(ControlServerTest, RefusesAWrongToken)
{
  RunServing([&]() -> Co<> {
    auto client = Client(io_);
    co_await client.Connect(server_->Port());

    const auto refusal
      = co_await client.Call(1, "rpc.hello", { { "token", "wrong" } });
    const auto closed = co_await client.Receive();

    EXPECT_EQ(refusal["error"]["code"], -32600);
    EXPECT_TRUE(closed.is_null());
  });
}

//! Subscribed topics are pushed as `rpc.event` notifications.
NOLINT_TEST_F(ControlServerTest, PushesSubscribedEvents)
{
  RunServing([&]() -> Co<> {
    auto client = Client(io_);
    co_await client.Connect(server_->Port());
    static_cast<void>(co_await client.Call(
      1, "rpc.hello", { { "token", Token() }, { "client", "test" } }));
    static_cast<void>(
      co_await client.Call(2, "rpc.subscribe", { { "topics", { "custom" } } }));

    events_.Publish("custom", json { { "value", 42 } });
    const auto event = co_await client.Receive();

    EXPECT_EQ(event["method"], "rpc.event");
    EXPECT_EQ(event["params"]["topic"], "custom");
    EXPECT_EQ(event["params"]["data"]["value"], 42);
    EXPECT_TRUE(events_.HasInterest("custom"));
  });
}

//! `rpc.cancel` cancels a command in flight, which replies `kCancelled`.
NOLINT_TEST_F(ControlServerTest, RpcCancelCancelsARequestInFlight)
{
  static_cast<void>(
    engine_.GetCommands().Register(oxygen::commands::CommandDefinition {
      .name = "test.forever",
      .label = "Forever",
      .help = "Never finishes",
      .async_handler
      = [](oxygen::commands::CommandArgs, oxygen::commands::CommandContext)
        -> Co<oxygen::commands::CommandResult> {
        // Never triggered: only cancellation ends the job.
        oxygen::co::Event never;
        co_await never;
        co_return oxygen::commands::CommandResult::Success("unreachable");
      },
    }));
  RunServing([&]() -> Co<> {
    auto client = Client(io_);
    co_await client.Connect(server_->Port());
    static_cast<void>(co_await client.Call(
      1, "rpc.hello", { { "token", Token() }, { "client", "test" } }));

    co_await client.Send(
      { { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "test.forever" } });
    co_await client.Send({
      { "jsonrpc", "2.0" },
      { "id", 3 },
      { "method", "rpc.cancel" },
      { "params", { { "id", 2 } } },
    });
    auto replies = std::map<int, json> {};
    while (replies.size() < 2U) {
      auto message = co_await client.Receive();
      if (message.is_null()) {
        break;
      }
      if (message.contains("id")) {
        // Read the id first: json's assignment takes its operand by value.
        const auto id = message["id"].get<int>();
        replies[id] = std::move(message);
      }
    }

    EXPECT_EQ(replies[3]["result"]["cancelled"], true);
    // Compared as json: a missing reply is null and fails without throwing.
    EXPECT_EQ(replies[2]["error"]["code"], json(-32003));
  });
}

//! A ninth connection is refused and closed.
NOLINT_TEST_F(ControlServerTest, RefusesConnectionsBeyondTheLimit)
{
  RunServing([&]() -> Co<> {
    auto clients = std::vector<std::unique_ptr<Client>> {};
    for (int i = 0; i < 8; ++i) {
      clients.push_back(std::make_unique<Client>(io_));
      co_await clients.back()->Connect(server_->Port());
    }
    auto extra = Client(io_);
    co_await extra.Connect(server_->Port());

    const auto refusal = co_await extra.Receive();
    const auto closed = co_await extra.Receive();

    EXPECT_EQ(refusal["error"]["code"], -32000);
    EXPECT_TRUE(closed.is_null());
  });
}

//! With `exit_when_owner_leaves`, the owner's disconnect posts `ngin.quit`
//! through the dispatcher, attributed to the owner.
NOLINT_TEST_F(ControlServerTest, OwnerLeavingPostsQuit)
{
  config_.exit_when_owner_leaves = true;
  RunServing([&]() -> Co<> {
    {
      auto owner = Client(io_);
      co_await owner.Connect(server_->Port());
      const auto hello = co_await owner.Call(1, "rpc.hello",
        { { "token", Token() }, { "client", "editor" }, { "owner", true } });
      EXPECT_EQ(hello["result"]["owner"], true);
      owner.Close();
    }
    for (int i = 0; i < 200 && !engine_.stopped; ++i) {
      auto timer = asio::steady_timer(io_, 1ms);
      co_await timer.async_wait(oxygen::co::asio_awaitable);
    }

    EXPECT_TRUE(engine_.stopped);
    const auto batch
      = events_.Since(0U, std::vector<std::string> { "command" });
    EXPECT_FALSE(batch.events.empty());
    if (!batch.events.empty()) {
      const auto& quit = batch.events.back().data;
      EXPECT_EQ(quit["commands"][0]["command"], "ngin.quit");
      EXPECT_EQ(quit["client"], "editor");
    }
  });
}

//! The discovery file exists while the server runs and goes with it.
NOLINT_TEST(ControlServerDiscovery, FileLivesWithTheServer)
{
  auto path = std::filesystem::path {};
  {
    asio::io_context io;
    TestEngine engine;
    EventLog events;
    CommandDispatcher dispatcher {
      observer_ptr<oxygen::IAsyncEngine> { &engine }, observer_ptr { &events }
    };
    ControlServer server { observer_ptr<oxygen::IAsyncEngine> { &engine },
      observer_ptr { &dispatcher }, observer_ptr { &events }, io,
      ControlServerConfig {} };
    oxygen::co::Run(io, [&]() -> Co<> {
      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&ControlServer::ActivateAsync, &server);
        server.Run();
        path = server.DiscoveryFile();
        EXPECT_TRUE(std::filesystem::exists(path));
        co_return oxygen::co::kCancel;
      };
    });
  }
  EXPECT_FALSE(path.empty());
  EXPECT_FALSE(std::filesystem::exists(path));
}

} // namespace
