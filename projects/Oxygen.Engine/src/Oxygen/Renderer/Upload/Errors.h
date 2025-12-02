//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <system_error>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

//! Domain-specific Upload error codes exposed as std::error_code.
enum class UploadError : int {
  kInvalidRequest,
  kStagingAllocFailed,
  kRecordingFailed,
  kResourceAllocFailed,
  kSubmitFailed,
  kDeviceLost,
  kProducerFailed,
  kCanceled,
  kStagingMapFailed,
  // Tracker-specific errors
  kTicketNotFound,
  kTrackerShutdown,
  // Planner-specific errors
  kUnsupportedFormat,
};

//! Category for upload errors.
class UploadErrorCategory : public std::error_category {
public:
  const char* name() const noexcept override { return "Upload Error"; }

  std::string message(int ev) const override
  {
    switch (static_cast<UploadError>(ev)) {
    case UploadError::kStagingMapFailed:
      return "Failed to map staging buffer memory for upload operation";
    case UploadError::kInvalidRequest:
      return "Upload request contains invalid parameters or resource "
             "descriptors";
    case UploadError::kStagingAllocFailed:
      return "Failed to allocate staging buffer memory for upload operation";
    case UploadError::kResourceAllocFailed:
      return "Failed to allocate GPU resource or create resource view for "
             "upload operation";
    case UploadError::kRecordingFailed:
      return "GPU command recording failed during upload preparation";
    case UploadError::kSubmitFailed:
      return "Failed to submit upload commands to GPU queue";
    case UploadError::kProducerFailed:
      return "Data producer callback failed to generate upload content";
    case UploadError::kCanceled:
      return "Upload operation was explicitly canceled before completion";
    case UploadError::kTicketNotFound:
      return "Upload ticket is invalid or has already been consumed";
    case UploadError::kTrackerShutdown:
      return "Upload tracker is shutting down and cannot process requests";
    default:
      return "Unknown upload subsystem error";
    }
  }
};

// Implement in .cpp to avoid multiple definitions so that we can reliably
// compare error_code for identity.
OXGN_RNDR_NDAPI const UploadErrorCategory& GetUploadErrorCategory() noexcept;

// Helper to create std::error_code from UploadError
inline std::error_code make_error_code(UploadError e) noexcept
{
  return { static_cast<int>(e), GetUploadErrorCategory() };
}

} // namespace oxygen::engine::upload

// Inject std::is_error_code_enum specialization into std so that
// UploadError can be implicitly converted to std::error_code.
template <>
struct std::is_error_code_enum<oxygen::engine::upload::UploadError>
  : true_type { };
