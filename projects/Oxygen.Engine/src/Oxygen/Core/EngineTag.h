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

class EngineTag final {
  friend struct internal::EngineTagFactory;
  EngineTag() noexcept = default;
  EngineTag(const EngineTag&) noexcept = default;

public:
  ~EngineTag() noexcept = default;
  EngineTag(EngineTag&&) noexcept = default;
  auto operator=(const EngineTag&) noexcept -> EngineTag& = delete;
  auto operator=(EngineTag&&) noexcept -> EngineTag& = delete;
};

} // namespace oxygen::engine
