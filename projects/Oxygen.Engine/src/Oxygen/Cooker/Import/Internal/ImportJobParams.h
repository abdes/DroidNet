//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stop_token>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>

namespace oxygen::co {
class Event;
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::serio {
class IoDriver;
} // namespace oxygen::serio

namespace oxygen::content::import {
class IAsyncFileReader;
class ResourceTableRegistry;
class LooseCookedIndexRegistry;
class ModelImportRecord;

namespace detail {

  //! Parameters for creating an import job.
  struct ImportJobParams {
    ImportJobId id;
    ImportRequest request;
    ImportCompletionCallback on_complete;
    ProgressEventCallback on_progress;
    std::shared_ptr<co::Event> cancel_event;
    observer_ptr<IAsyncFileReader> reader;
    //! The driver of the import loop: cooked writes are awaited on it.
    observer_ptr<serio::IoDriver> driver;
    observer_ptr<co::ThreadPool> thread_pool;
    observer_ptr<ResourceTableRegistry> registry;
    observer_ptr<LooseCookedIndexRegistry> index_registry;
    ImportConcurrency concurrency;
    AsyncImportService::ScriptCompileCallback script_compile_callback;
    std::stop_token stop_token;
    std::shared_ptr<ModelImportRecord> model_import_record;
  };

} // namespace detail
} // namespace oxygen::content::import
