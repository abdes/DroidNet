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

//! Routes SIGINT, SIGTERM and SIGBREAK to the stop state read by
//! `StartStopWatcher`.
/*!
 The stop state starts here and lasts for the process, so a signal that arrives
 before the service exists still stops the run. The first signal asks the
 running import to stop; the third ends the process with 128 plus the signal.
*/
auto InstallStopHandlers() -> void;

//! Watches for a stop request, past or future, and cancels `service` on one.
[[nodiscard]] auto StartStopWatcher(AsyncImportService* service,
  oxygen::observer_ptr<IMessageWriter> writer) -> std::jthread;

} // namespace oxygen::content::import::tool
