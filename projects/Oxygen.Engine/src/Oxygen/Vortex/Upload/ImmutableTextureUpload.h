//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class CommandRecorder;
}
namespace oxygen::vortex::upload {
class UploadCoordinator;

//! Prepared immutable texture data; the caller records and submits explicitly.
class ImmutableTextureUpload final {
public:
  ~ImmutableTextureUpload() = default;
  OXYGEN_MAKE_NON_COPYABLE(ImmutableTextureUpload)
  OXYGEN_DEFAULT_MOVABLE(ImmutableTextureUpload)

  //! Record once on a graphics queue. Discard the recording on failure.
  OXGN_VRTX_NDAPI auto Record(graphics::CommandRecorder& recorder)
    -> Result<void, UploadError>;

  //! Keep an owner/lease and the successful submission receipt when publishing.
  [[nodiscard]] auto Destination() const noexcept
    -> const graphics::ManagedTexture&
  {
    return destination_;
  }

private:
  friend class UploadCoordinator;
  ImmutableTextureUpload(std::weak_ptr<Graphics> graphics,
    graphics::ManagedTexture destination, graphics::ManagedBuffer staging,
    std::vector<graphics::TextureUploadRegion> regions);

  std::weak_ptr<Graphics> graphics_;
  graphics::ManagedTexture destination_;
  graphics::ManagedBuffer staging_;
  std::vector<graphics::TextureUploadRegion> regions_;
  bool recorded_ { false };
};
} // namespace oxygen::vortex::upload
