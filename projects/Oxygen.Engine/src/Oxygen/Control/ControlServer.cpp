//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <random>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Control/CommandDispatcher.h>
#include <Oxygen/Control/ControlServer.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/asio.h>

namespace oxygen::control {

namespace {

  using nlohmann::json;
  using tcp = asio::ip::tcp;

  constexpr int kProtocolVersion = 1;
  constexpr std::size_t kMaxLineBytes = 1024U * 1024U;
  constexpr auto kOwnerGrace = std::chrono::seconds(10);
  constexpr std::size_t kTokenBytes = 32U;
  constexpr std::size_t kMaxEventsPerRequest = 1000U;
  constexpr std::size_t kMaxConnections = 8U;

  // JSON-RPC 2.0 error codes, and the server's own for command outcomes.
  constexpr int kParseError = -32700;
  constexpr int kInvalidRequest = -32600;
  constexpr int kMethodNotFound = -32601;
  constexpr int kInvalidParams = -32602;
  constexpr int kCommandError = -32000;
  constexpr int kCommandDisabled = -32001;
  constexpr int kCommandDenied = -32002;
  constexpr int kCommandCancelled = -32003;

  auto ProcessId() -> long long
  {
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
  }

  auto MakeToken() -> std::string
  {
    auto device = std::random_device {};
    auto token = std::string {};
    token.reserve(kTokenBytes * 2U);
    for (std::size_t i = 0; i < kTokenBytes; ++i) {
      token += fmt::format("{:02x}", device() & 0xFFU);
    }
    return token;
  }

  //! `%LOCALAPPDATA%/Oxygen/control` on Windows, else under XDG_RUNTIME_DIR
  //! or the temporary directory.
  auto DiscoveryFolder() -> std::filesystem::path
  {
    auto root = std::string {};
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0U;
    if (_dupenv_s(&value, &length, "LOCALAPPDATA") == 0 && value != nullptr) {
      root = value;
    }
    std::free(value); // NOLINT(*-no-malloc,*-owning-memory) - _dupenv_s owns it
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe) - read once at startup
    if (const auto* const value = std::getenv("XDG_RUNTIME_DIR")) {
      root = value;
    }
#endif
    const auto base = root.empty() ? std::filesystem::temp_directory_path()
                                   : std::filesystem::path(root);
    return base / "Oxygen" / "control";
  }

  auto ErrorCodeFor(const commands::ExecutionStatus status) -> int
  {
    switch (status) {
    case commands::ExecutionStatus::kNotFound:
      return kMethodNotFound;
    case commands::ExecutionStatus::kInvalidArguments:
      return kInvalidParams;
    case commands::ExecutionStatus::kDisabled:
      return kCommandDisabled;
    case commands::ExecutionStatus::kDenied:
      return kCommandDenied;
    case commands::ExecutionStatus::kCancelled:
      return kCommandCancelled;
    case commands::ExecutionStatus::kOk:
    case commands::ExecutionStatus::kError:
    case commands::ExecutionStatus::kPending:
      break;
    }
    return kCommandError;
  }

  auto Reply(const json& id, json result) -> json
  {
    return { { "jsonrpc", "2.0" }, { "id", id },
      { "result", std::move(result) } };
  }

  auto ErrorReply(const json& id, const int code, std::string message,
    json data = nullptr) -> json
  {
    auto error = json { { "code", code }, { "message", std::move(message) } };
    if (!data.is_null()) {
      error["data"] = std::move(data);
    }
    return { { "jsonrpc", "2.0" }, { "id", id },
      { "error", std::move(error) } };
  }

  auto Dump(const json& message) -> std::string
  {
    return message.dump(-1, ' ', false, json::error_handler_t::replace);
  }

  auto EventJson(const ControlEvent& event) -> json
  {
    return {
      { "seq", event.seq },
      { "frame", event.frame },
      { "topic", event.topic },
      { "data", event.data },
    };
  }

  auto TopicsOf(const json& params) -> std::vector<std::string>
  {
    auto topics = std::vector<std::string> {};
    if (params.is_object() && params.contains("topics")
      && params["topics"].is_array()) {
      for (const auto& topic : params["topics"]) {
        if (topic.is_string()) {
          topics.push_back(topic.get<std::string>());
        }
      }
    }
    return topics;
  }

  auto ResultReply(const json& id, const commands::CommandResult& result)
    -> json
  {
    if (result.status == commands::ExecutionStatus::kOk) {
      return Reply(
        id, { { "output", result.output }, { "data", result.data } });
    }
    auto data = result.data.is_null() ? json::object() : result.data;
    data["status"] = commands::to_string(result.status);
    data["output"] = result.output;
    return ErrorReply(
      id, ErrorCodeFor(result.status), result.error, std::move(data));
  }

} // namespace

struct ControlServer::Impl {
  Impl(observer_ptr<IAsyncEngine> in_engine,
    observer_ptr<CommandDispatcher> in_dispatcher,
    observer_ptr<EventLog> in_events, asio::io_context& in_io,
    ControlServerConfig in_config)
    : engine(in_engine)
    , dispatcher(in_dispatcher)
    , events(in_events)
    , io(in_io)
    , config(std::move(in_config))
    , acceptor(in_io)
    , token(MakeToken())
  {
  }

  observer_ptr<IAsyncEngine> engine;
  observer_ptr<CommandDispatcher> dispatcher;
  observer_ptr<EventLog> events;
  asio::io_context& io;
  //! Wakes connections' event pushers on the engine thread.
  co::ParkingLot events_ready;
  std::atomic_bool wake_pending { false };
  ControlServerConfig config;
  tcp::acceptor acceptor;
  std::string token;
  std::filesystem::path discovery_file;
  uint16_t port { 0U };
  std::size_t connections { 0U };
  bool owner_connected { false };

  //! Quits through the dispatcher, like the window's close button.
  auto PostQuit(commands::CommandContext context) const -> void
  {
    auto quit = engine->GetCommands().BindJson("ngin.quit", json::object());
    if (!quit) {
      LOG_F(ERROR, "control server: cannot post ngin.quit ({}); stopping",
        quit.error().error);
      engine->Stop();
      return;
    }
    static_cast<void>(
      dispatcher->Post(std::move(quit).value(), std::move(context)));
  }
};

struct ControlServer::Connection {
  explicit Connection(asio::io_context& io)
    : socket(io)
  {
  }

  tcp::socket socket;
  std::deque<std::string> outbox;
  co::ParkingLot outbox_ready;
  std::string client;
  //! Subscribed topics, and the last event sequence number pushed.
  std::vector<std::string> topics;
  uint64_t cursor { 0U };
  //! Commands in flight, by request id, for `rpc.cancel`.
  std::unordered_map<std::string, std::shared_ptr<co::Event>> in_flight;
  bool authenticated { false };
  bool owner { false };
  bool closed { false };

  auto Send(const json& message) -> void
  {
    if (closed) {
      return;
    }
    outbox.push_back(Dump(message) + "\n");
    outbox_ready.UnParkAll();
  }
};

ControlServer::ControlServer(const observer_ptr<IAsyncEngine> engine,
  const observer_ptr<CommandDispatcher> dispatcher,
  const observer_ptr<EventLog> events, asio::io_context& io,
  ControlServerConfig config)
  : impl_(
      std::make_unique<Impl>(engine, dispatcher, events, io, std::move(config)))
{
  CHECK_NOTNULL_F(engine);
  CHECK_NOTNULL_F(dispatcher);
  CHECK_NOTNULL_F(events);
}

ControlServer::~ControlServer()
{
  impl_->events->SetPublishedHandler({});
  impl_->events->StopLogCapture();
  if (!impl_->discovery_file.empty()) {
    auto error = std::error_code {};
    std::filesystem::remove(impl_->discovery_file, error);
  }
}

auto ControlServer::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  return OpenNursery(nursery_, std::move(started));
}

auto ControlServer::Run() -> void
{
  CHECK_NOTNULL_F(nursery_, "activate the control server before running it");
  auto& acceptor = impl_->acceptor;
  const auto endpoint
    = tcp::endpoint(asio::ip::address_v4::loopback(), impl_->config.port);
  acceptor.open(endpoint.protocol());
  acceptor.set_option(tcp::acceptor::reuse_address(false));
  acceptor.bind(endpoint);
  acceptor.listen();
  impl_->port = acceptor.local_endpoint().port();

  const auto folder = DiscoveryFolder();
  std::filesystem::create_directories(folder);
  impl_->discovery_file = folder / fmt::format("{}.json", ProcessId());
  {
    auto file = std::ofstream(impl_->discovery_file, std::ios::trunc);
    file << json {
      { "protocol", kProtocolVersion },
      { "pid", ProcessId() },
      { "application", impl_->config.application },
      { "port", impl_->port },
      { "token", impl_->token },
    }
              .dump(2);
  }
  LOG_F(INFO, "control server listening on 127.0.0.1:{} ({})", impl_->port,
    impl_->discovery_file.string());

  // Events are published from any thread; pushing them happens on the
  // engine thread, from the I/O context the host loop polls.
  impl_->events->SetPublishedHandler([impl = impl_.get()] -> void {
    if (!impl->wake_pending.exchange(true)) {
      asio::post(impl->io, [impl] -> void {
        impl->wake_pending.store(false);
        impl->events_ready.UnParkAll();
      });
    }
  });
  impl_->events->StartLogCapture();

  nursery_->Start(&ControlServer::AcceptLoop, this);
  if (impl_->config.exit_when_owner_leaves) {
    nursery_->Start(&ControlServer::OwnerGrace, this);
  }
}

auto ControlServer::Stop() -> void
{
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }
}

auto ControlServer::Port() const noexcept -> uint16_t { return impl_->port; }

auto ControlServer::DiscoveryFile() const -> const std::filesystem::path&
{
  return impl_->discovery_file;
}

auto ControlServer::AcceptLoop() -> co::Co<>
{
  for (;;) {
    auto connection = std::make_shared<Connection>(impl_->io);
    const auto error = co_await impl_->acceptor.async_accept(
      connection->socket, co::asio_nothrow_awaitable);
    if (co::IsOperationAborted(error) || !impl_->acceptor.is_open()) {
      // Stopping aborts the pending accept; a closed acceptor would fail every
      // further accept at once.
      DLOG_F(1, "control server: stopped accepting connections");
      co_return;
    }
    if (error) {
      LOG_F(WARNING, "control server: accept failed: {}", error.message());
      continue;
    }
    if (impl_->connections >= kMaxConnections) {
      LOG_F(WARNING, "control server: refused a connection; {} are open",
        impl_->connections);
      const auto refusal
        = Dump(ErrorReply(nullptr, kCommandError,
            fmt::format("too many connections (at most {})", kMaxConnections)))
        + '\n';
      static_cast<void>(co_await asio::async_write(
        connection->socket, asio::buffer(refusal), co::asio_nothrow_awaitable));
      auto ignored = std::error_code {};
      connection->socket.close(ignored);
      continue;
    }
    nursery_->Start(&ControlServer::Serve, this, std::move(connection));
  }
}

auto ControlServer::OwnerGrace() -> co::Co<>
{
  auto timer = asio::steady_timer(impl_->io, kOwnerGrace);
  static_cast<void>(co_await timer.async_wait(co::asio_nothrow_awaitable));
  if (!impl_->owner_connected) {
    LOG_F(WARNING, "control server: no owner connected within {}s; quitting",
      kOwnerGrace.count());
    impl_->PostQuit({ .source = commands::CommandSource::kAutomation });
  }
}

auto ControlServer::Serve(std::shared_ptr<Connection> connection) -> co::Co<>
{
  ++impl_->connections;
  const auto count = ScopeGuard(
    [impl = impl_.get()] noexcept -> void { --impl->connections; });
  OXCO_WITH_NURSERY(n)
  {
    n.Start([connection]() -> co::Co<> {
      for (;;) {
        while (connection->outbox.empty()) {
          co_await connection->outbox_ready.Park();
        }
        const auto line = std::move(connection->outbox.front());
        connection->outbox.pop_front();
        const auto [error, written] = co_await asio::async_write(
          connection->socket, asio::buffer(line), co::asio_nothrow_awaitable);
        if (error) {
          co_return;
        }
      }
    });

    auto pending = std::string {};
    for (;;) {
      const auto [error, length] = co_await asio::async_read_until(
        connection->socket, asio::dynamic_buffer(pending, kMaxLineBytes), '\n',
        co::asio_nothrow_awaitable);
      if (error) {
        break;
      }
      auto line = pending.substr(0U, length - 1U);
      pending.erase(0U, length);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty()) {
        continue;
      }
      if (!connection->authenticated) {
        // The handshake runs inline: nothing else may run before it.
        auto message = json::parse(line, nullptr, false);
        const auto id = message.is_object() && message.contains("id")
          ? message["id"]
          : json {};
        const bool is_hello = message.is_object()
          && message.value("method", std::string {}) == "rpc.hello";
        const auto params
          = is_hello ? message.value("params", json::object()) : json::object();
        if (!is_hello
          || params.value("token", std::string {}) != impl_->token) {
          const auto refusal = ErrorReply(id, kInvalidRequest,
                                 "the first message must be rpc.hello with "
                                 "the token from the discovery file")
                                 .dump()
            + '\n';
          static_cast<void>(co_await asio::async_write(connection->socket,
            asio::buffer(refusal), co::asio_nothrow_awaitable));
          break;
        }
        connection->authenticated = true;
        connection->client = params.value("client", std::string { "client" });
        if (params.value("owner", false) && !impl_->owner_connected) {
          connection->owner = true;
          impl_->owner_connected = true;
        }
        connection->Send(Reply(id,
          {
            { "protocol", kProtocolVersion },
            { "application", impl_->config.application },
            { "pid", ProcessId() },
            { "owner", connection->owner },
            { "event_seq", impl_->events->LatestSeq() },
          }));
        n.Start(&ControlServer::PushEvents, this, connection);
        LOG_F(INFO, "control server: '{}' connected{}", connection->client,
          connection->owner ? " as owner" : "");
        continue;
      }
      // Requests outlive their connection: a disconnect abandons the reply,
      // not the command.
      nursery_->Start(
        &ControlServer::Handle, this, connection, std::move(line));
    }
    co_return co::kCancel;
  };

  connection->closed = true;
  for (const auto& topic : connection->topics) {
    impl_->events->RemoveInterest(topic);
  }
  connection->topics.clear();
  auto ignored = std::error_code {};
  connection->socket.close(ignored);
  LOG_F(INFO, "control server: '{}' disconnected", connection->client);
  if (connection->owner) {
    impl_->owner_connected = false;
    if (impl_->config.exit_when_owner_leaves) {
      LOG_F(INFO, "control server: the owner left; quitting");
      impl_->PostQuit({
        .source = commands::CommandSource::kRemote,
        .client = connection->client,
      });
    }
  }
}

auto ControlServer::Handle(
  std::shared_ptr<Connection> connection, std::string line) -> co::Co<>
{
  auto message = json::parse(line, nullptr, false);
  if (message.is_discarded()) {
    connection->Send(ErrorReply(nullptr, kParseError, "not valid JSON"));
    co_return;
  }
  const auto id
    = message.is_object() && message.contains("id") ? message["id"] : json {};
  if (!message.is_object() || message.value("jsonrpc", std::string {}) != "2.0"
    || !message.contains("method") || !message["method"].is_string()) {
    connection->Send(
      ErrorReply(id, kInvalidRequest, "not a JSON-RPC 2.0 request"));
    co_return;
  }
  const auto method = message["method"].get<std::string>();
  const auto params = message.value("params", json {});

  // Connection plumbing; everything else is a command.
  if (method == "rpc.subscribe") {
    for (auto& topic : TopicsOf(params)) {
      if (!std::ranges::contains(connection->topics, topic)) {
        if (connection->topics.empty()) {
          connection->cursor = impl_->events->LatestSeq();
        }
        impl_->events->AddInterest(topic);
        connection->topics.push_back(std::move(topic));
      }
    }
    connection->Send(Reply(id, { { "topics", connection->topics } }));
    co_return;
  }
  if (method == "rpc.unsubscribe") {
    for (const auto& topic : TopicsOf(params)) {
      if (const auto it = std::ranges::find(connection->topics, topic);
        it != connection->topics.end()) {
        impl_->events->RemoveInterest(topic);
        connection->topics.erase(it);
      }
    }
    connection->Send(Reply(id, { { "topics", connection->topics } }));
    co_return;
  }
  if (method == "rpc.events") {
    const auto since = params.is_object()
      ? params.value("since", uint64_t { 0U })
      : uint64_t { 0U };
    const auto limit = (std::min)(params.is_object()
        ? params.value("limit", kMaxEventsPerRequest)
        : kMaxEventsPerRequest,
      kMaxEventsPerRequest);
    const auto topics = TopicsOf(params);
    const auto batch = impl_->events->Since(since, topics, limit);
    auto events = json::array();
    for (const auto& event : batch.events) {
      events.push_back(EventJson(event));
    }
    connection->Send(Reply(id,
      {
        { "events", std::move(events) },
        { "next_seq", batch.next_seq },
        { "dropped", batch.dropped },
      }));
    co_return;
  }

  if (method == "rpc.cancel") {
    const auto target = params.is_object() && params.contains("id")
      ? params["id"].dump()
      : std::string {};
    const auto it = connection->in_flight.find(target);
    const bool cancelled = it != connection->in_flight.end();
    if (cancelled) {
      it->second->Trigger();
    }
    connection->Send(Reply(id, { { "cancelled", cancelled } }));
    co_return;
  }

  auto invocation = impl_->engine->GetCommands().BindJson(method, params);
  if (!invocation) {
    connection->Send(ResultReply(id, invocation.error()));
    co_return;
  }
  auto run = impl_->dispatcher->Run(std::move(invocation).value(),
    commands::CommandContext {
      .source = commands::CommandSource::kRemote,
      .client = connection->client,
    });
  if (id.is_null()) {
    // A notification: the command runs, nothing is replied or cancellable.
    static_cast<void>(co_await std::move(run));
    co_return;
  }
  const auto key = id.dump();
  if (connection->in_flight.contains(key)) {
    connection->Send(ErrorReply(id, kInvalidRequest,
      "a request with this id is still in flight on this connection"));
    co_return;
  }
  const auto cancel = std::make_shared<co::Event>();
  connection->in_flight.emplace(key, cancel);
  const auto forget = ScopeGuard(
    [connection, key] noexcept -> void { connection->in_flight.erase(key); });
  // Abandoning the dispatcher's awaiter cancels the job.
  const auto outcome = co_await co::AnyOf(std::move(run), *cancel);
  const auto& result = std::get<0>(outcome);
  connection->Send(ResultReply(id,
    result
      ? *result
      : commands::CommandResult::Failure(
          commands::ExecutionStatus::kCancelled, "cancelled by rpc.cancel")));
}

auto ControlServer::PushEvents(std::shared_ptr<Connection> connection)
  -> co::Co<>
{
  for (;;) {
    co_await impl_->events_ready.Park();
    if (connection->topics.empty()) {
      continue;
    }
    const auto batch
      = impl_->events->Since(connection->cursor, connection->topics);
    connection->cursor = batch.next_seq - 1U;
    for (const auto& event : batch.events) {
      connection->Send({
        { "jsonrpc", "2.0" },
        { "method", "rpc.event" },
        { "params", EventJson(event) },
      });
    }
  }
}

} // namespace oxygen::control
