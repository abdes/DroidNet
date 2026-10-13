//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Control/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace asio {
class io_context;
} // namespace asio

namespace oxygen {
class IAsyncEngine;
} // namespace oxygen

namespace oxygen::control {

class CommandDispatcher;
class EventLog;

struct ControlServerConfig {
  //! Loopback TCP port; 0 takes an ephemeral one.
  uint16_t port { 0U };
  //! Reported by `rpc.hello` and in the discovery file.
  std::string application;
  //! Stop the engine when the owning client disconnects, or when no owner
  //! connects within the grace period.
  bool exit_when_owner_leaves { false };
  //! Label the launcher gives this instance, so a client can tell instances
  //! of one application apart; empty for none.
  std::string name;
  //! Folder of the discovery file; empty for the per-user folder.
  std::filesystem::path discovery_folder;
};

//! JSON-RPC 2.0 control server: every command is a method.
/*!
 One JSON object per line over loopback TCP. The first message on a
 connection must be `rpc.hello` with the token from the discovery file;
 every other method is a command name, run through the `CommandDispatcher`,
 and replied to when the command finishes.

 The server is a `co::LiveObject` whose tasks are OxCo coroutines over Asio
 operations on the platform's `io_context`, which the host loop polls on the
 engine thread; there is no thread of its own.
*/
class ControlServer final : public co::LiveObject {
public:
  OXGN_CTRL_API ControlServer(observer_ptr<IAsyncEngine> engine,
    observer_ptr<CommandDispatcher> dispatcher, observer_ptr<EventLog> events,
    asio::io_context& io, ControlServerConfig config);
  OXGN_CTRL_API ~ControlServer() override;

  OXYGEN_MAKE_NON_COPYABLE(ControlServer)
  OXYGEN_MAKE_NON_MOVABLE(ControlServer)

  OXGN_CTRL_NDAPI auto ActivateAsync(co::TaskStarted<> started)
    -> co::Co<> override;
  //! Listens, writes the discovery file and starts accepting connections.
  OXGN_CTRL_API auto Run() -> void override;
  OXGN_CTRL_API auto Stop() -> void override;
  OXGN_CTRL_NDAPI auto IsRunning() const -> bool override
  {
    return nursery_ != nullptr;
  }

  //! The port listened on; valid after `Run`.
  OXGN_CTRL_NDAPI auto Port() const noexcept -> uint16_t;
  //! Where the endpoint and token are published for clients.
  OXGN_CTRL_NDAPI auto DiscoveryFile() const -> const std::filesystem::path&;

private:
  struct Impl;
  struct Connection;

  auto AcceptLoop() -> co::Co<>;
  auto Serve(std::shared_ptr<Connection> connection) -> co::Co<>;
  auto Handle(std::shared_ptr<Connection> connection, std::string line)
    -> co::Co<>;
  auto PushEvents(std::shared_ptr<Connection> connection) -> co::Co<>;
  auto OwnerGrace() -> co::Co<>;

  std::unique_ptr<Impl> impl_;
  co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen::control
