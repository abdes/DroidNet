//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::graphics {

struct TextureViewRequest {
  TextureViewDescription description;
  std::optional<bindless::DomainToken> domain;
};

struct BufferViewRequest {
  BufferViewDescription description;
  std::optional<bindless::DomainToken> domain;
};

//! Internal allocation ownership. Recording sites retain their own GPU uses.
struct ManagedTexture {
  ManagedTexture() = default;
  ~ManagedTexture() = default;
  OXYGEN_MAKE_NON_COPYABLE(ManagedTexture)
  OXYGEN_DEFAULT_MOVABLE(ManagedTexture)
  std::shared_ptr<Texture> resource;
  RegistrationOwner registration;
  std::vector<ManagedView> views;
};

struct ManagedBuffer {
  ManagedBuffer() = default;
  ~ManagedBuffer() = default;
  OXYGEN_MAKE_NON_COPYABLE(ManagedBuffer)
  OXYGEN_DEFAULT_MOVABLE(ManagedBuffer)
  std::shared_ptr<Buffer> resource;
  RegistrationOwner registration;
  std::vector<ManagedView> views;
};

} // namespace oxygen::graphics
