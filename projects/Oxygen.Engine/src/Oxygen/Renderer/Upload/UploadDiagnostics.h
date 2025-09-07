//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

struct UploadStats {
  uint64_t submitted { 0 };
  uint64_t completed { 0 };
  uint64_t in_flight { 0 };
  uint64_t bytes_submitted { 0 };
  uint64_t bytes_completed { 0 };
};

} // namespace oxygen::engine::upload
