//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <thread>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::content::import {
class AsyncImportService;
} // namespace oxygen::content::import

namespace oxygen::content::import::tool {

class IMessageWriter;

//! Routes SIGINT and SIGTERM to the stop state read by `StartStopWatcher`.
/*!
 The first signal asks the running import to stop; the third ends the process.
*/
auto InstallStopHandlers() -> void;

//! Forgets any earlier stop request.
auto ResetStopState() -> void;

//! Watches for a stop request and cancels `service` when one arrives.
[[nodiscard]] auto StartStopWatcher(AsyncImportService* service,
  oxygen::observer_ptr<IMessageWriter> writer) -> std::jthread;

} // namespace oxygen::content::import::tool
