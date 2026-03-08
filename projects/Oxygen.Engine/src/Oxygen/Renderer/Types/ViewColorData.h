//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>

namespace oxygen::engine {

//! Shared per-view color/presentation data consumed across renderer systems.
struct alignas(16) ViewColorData {
  float exposure { 1.0F };
  std::array<float, 3> _pad_to_16 {};
};

static_assert(sizeof(ViewColorData) == 16);
static_assert(alignof(ViewColorData) == 16);

} // namespace oxygen::engine
