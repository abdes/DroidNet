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
  ImportPipelineConcurrency mesh_build { .workers = 2, .queue_capacity = 32 };
  ImportPipelineConcurrency geometry { .workers = 1, .queue_capacity = 16 };
  ImportPipelineConcurrency scene { .workers = 1, .queue_capacity = 8 };
};

//! Whether every pipeline can hold work: a zero capacity never takes any.
[[nodiscard]] constexpr auto AllQueuesHaveCapacity(
  const ImportConcurrency& concurrency) noexcept -> bool
{
  return concurrency.texture.queue_capacity > 0U
    && concurrency.buffer.queue_capacity > 0U
    && concurrency.material.queue_capacity > 0U
    && concurrency.mesh_build.queue_capacity > 0U
    && concurrency.geometry.queue_capacity > 0U
    && concurrency.scene.queue_capacity > 0U;
}

} // namespace oxygen::content::import
