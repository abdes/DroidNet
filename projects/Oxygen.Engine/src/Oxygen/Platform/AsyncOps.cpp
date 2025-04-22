//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <csignal>
#include <system_error>
#include <type_traits>

#include <asio/signal_set.hpp>

#include "Platform.h"
#include <Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::platform::AsyncOps;

void AsyncOps::HandleSignal(const std::error_code& error, int signal_number)
{
    if (error) {
        LOG_F(ERROR, "Signal handler error: {}", error.message());
        return;
    }

    signals_.async_wait(
        [this](const std::error_code& error, int signal_number) {
            HandleSignal(error, signal_number);
        });

    switch (signal_number) {
    case SIGINT:
        LOG_F(INFO, "Received SIGINT");
        break;

    case SIGTERM:
        LOG_F(INFO, "Received SIGTERM");
        break;

    default:
        LOG_F(1, "Received signal `{}` (unhandled)", signal_number);
        return;
    }

    // Trigger the termination event
    terminate_.Trigger();
}

AsyncOps::AsyncOps()
    : io_()
    , signals_(io_, SIGINT, SIGTERM)
{
}

oxygen::platform::AsyncOps::~AsyncOps()
{
    DLOG_IF_F(WARNING, (nursery_ != nullptr),
        "LiveObject destructor called while nursery is still open. "
        "Did you forget to call Stop() on the LiveObject?");
}

auto AsyncOps::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
    signals_.async_wait(
        [this](const std::error_code& error, int signal_number) {
            HandleSignal(error, signal_number);
        });

    return OpenNursery(nursery_, std::move(started));
}

void AsyncOps::Stop()
{
    io_.stop();
    if (nursery_ != nullptr) {
        nursery_->Cancel();
    }
}
