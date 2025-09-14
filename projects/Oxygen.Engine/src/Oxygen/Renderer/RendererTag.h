//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/*!
 @file RendererTag.h

 @brief RendererTag is a capability token that only engine-internal code can
 construct.

 The engine exposes a class-based factory in the `internal` namespace. The
 factory `Get()` method can only be implemented in one translation unit
 (guaranteed by the language), and that single implementation provides a
 controlled way to create RendererTag instances, ensuring that only
 engine-internal code can obtain them.

 @note Implementation is already included in the `Renderer.cpp` file.
*/

namespace oxygen::renderer {

namespace internal {
  struct RendererTagFactory;
} // namespace internal

class RendererTag {
  friend struct internal::RendererTagFactory;
  RendererTag() noexcept = default;
};

namespace internal {
  struct RendererTagFactory {
    static auto Get() noexcept -> RendererTag;
  };
} // namespace internal

} // namespace oxygen::renderer
