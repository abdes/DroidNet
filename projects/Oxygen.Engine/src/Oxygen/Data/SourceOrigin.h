//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::data {

//! Runtime identity of one source opening; never serialized into cooked data.
using SourceInstanceId = NamedType<uint64_t, struct SourceInstanceIdTag,
  DefaultInitialized, Comparable, Hashable, Printable>;

struct SourceOrigin final {
  SourceKey key {};
  SourceInstanceId instance {};

  auto operator==(const SourceOrigin&) const -> bool = default;
};

} // namespace oxygen::data
