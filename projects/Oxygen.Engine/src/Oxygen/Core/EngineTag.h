//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/*!
 @file EngineTag.h

 @brief EngineTag is a capability token that only engine-internal code can
 construct.

 The engine exposes a class-based factory in the `internal` namespace. The
 factory `Get()` method can only be implemented in one translation unit
 (guaranteed by the language), and that single implementation provides a
 controlled way to create EngineTag instances, ensuring that only
 engine-internal code can obtain them.

 Implementation is already included in the `AsyncEngine.cpp` file.
*/

namespace oxygen::engine {

namespace internal {
  struct EngineTagFactory;
} // namespace internal

class EngineTag {
  friend struct internal::EngineTagFactory;
  EngineTag() noexcept = default;
};

namespace internal {
  struct EngineTagFactory {
    static auto Get() noexcept -> EngineTag;
  };
} // namespace internal

} // namespace oxygen::engine
