//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::content::import {

//! Per-pipeline concurrency settings.
struct ImportPipelineConcurrency {
  //! Number of worker coroutines to start for the pipeline.
  uint32_t workers = 2;

  //! Bounded capacity of the pipeline work queues.
  uint32_t queue_capacity = 64;
};

//! Concurrency tuning for async import pipelines.
struct ImportConcurrency {
  ImportPipelineConcurrency texture { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency buffer { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency material { .workers = 2, .queue_capacity = 64 };
  ImportPipelineConcurrency geometry { .workers = 2, .queue_capacity = 32 };
  ImportPipelineConcurrency scene { .workers = 1, .queue_capacity = 8 };
};

} // namespace oxygen::content::import
