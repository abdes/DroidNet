//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stop_token>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportConcurrency.h>
#include <Oxygen/Content/Import/ImportJobId.h>
#include <Oxygen/Content/Import/ImportRequest.h>

namespace oxygen::co {
class Event;
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::content::import {
class IAsyncFileReader;
class IAsyncFileWriter;
class ResourceTableRegistry;
class LooseCookedIndexRegistry;

namespace detail {

  //! Parameters for creating an import job.
  struct ImportJobParams {
    ImportJobId id;
    ImportRequest request;
    ImportCompletionCallback on_complete;
    ProgressEventCallback on_progress;
    std::shared_ptr<co::Event> cancel_event;
    observer_ptr<IAsyncFileReader> reader;
    observer_ptr<IAsyncFileWriter> writer;
    observer_ptr<co::ThreadPool> thread_pool;
    observer_ptr<ResourceTableRegistry> registry;
    observer_ptr<LooseCookedIndexRegistry> index_registry;
    ImportConcurrency concurrency;
    std::stop_token stop_token;
  };

} // namespace detail
} // namespace oxygen::content::import
