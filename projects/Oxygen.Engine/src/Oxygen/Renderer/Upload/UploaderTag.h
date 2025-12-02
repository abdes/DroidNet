//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

/*!
 @file UploaderTag.h

 @brief UploaderTag is a capability token that only engine-internal code can
 construct.

 The engine exposes a class-based factory in the `internal` namespace. The
 factory `Get()` method can only be implemented in one translation unit
 (guaranteed by the language), and that single implementation provides a
 controlled way to create UploaderTag instances, ensuring that only
 engine-internal code can obtain them.

 @note Implementation is already included in the `UploadCoordinator.cpp` file.
*/

namespace oxygen::engine::upload {

namespace internal {
  struct UploaderTagFactory;
} // namespace internal

class UploaderTag {
  friend struct internal::UploaderTagFactory;
  UploaderTag() noexcept = default;
};

namespace internal {
  struct UploaderTagFactory {
    static auto Get() noexcept -> UploaderTag;
  };
} // namespace internal

namespace internal {
  struct InlineCoordinatorTagFactory;
} // namespace internal

class InlineCoordinatorTag {
  friend struct internal::InlineCoordinatorTagFactory;
  InlineCoordinatorTag() noexcept = default;
};

namespace internal {
  struct InlineCoordinatorTagFactory {
    static auto Get() noexcept -> InlineCoordinatorTag;
  };
} // namespace internal

} // namespace oxygen::engine::upload
