//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <optional>

#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

OXGN_GFX_API auto ValidateTextureReadbackRequest(const TextureDesc& desc,
  const TextureReadbackRequest& request) -> std::expected<void, ReadbackError>;

namespace detail {

  //! Shared buffer-reuse state gate, applied after a nonblocking tracker
  //! refresh.
  [[nodiscard]] inline auto ValidateBufferReadbackReuseState(
    const ReadbackState state, const std::optional<ReadbackError> last_error)
    -> std::expected<void, ReadbackError>
  {
    switch (state) {
    case ReadbackState::kPending:
      return std::unexpected(ReadbackError::kAlreadyPending);
    case ReadbackState::kMapped:
      return std::unexpected(ReadbackError::kAlreadyMapped);
    case ReadbackState::kCancelled:
      return std::unexpected(ReadbackError::kCancelled);
    case ReadbackState::kFailed:
      return std::unexpected(
        last_error.value_or(ReadbackError::kBackendFailure));
    case ReadbackState::kIdle:
      return std::unexpected(ReadbackError::kNotReady);
    case ReadbackState::kReady:
      return {};
    }
    return std::unexpected(ReadbackError::kBackendFailure);
  }

} // namespace detail

} // namespace oxygen::graphics
