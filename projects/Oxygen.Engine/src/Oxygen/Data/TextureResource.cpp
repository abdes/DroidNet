//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/TextureResource.h>

using oxygen::data::TextureResource;

auto TextureResource::GetTextureType() const noexcept -> TextureType
{
  if (desc_.texture_type
      >= static_cast<std::underlying_type_t<TextureType>>(TextureType::kUnknown)
    && desc_.texture_type <= static_cast<std::underlying_type_t<TextureType>>(
         TextureType::kMaxTextureType)) {
    return static_cast<TextureType>(desc_.texture_type);
  }
  LOG_F(WARNING, "Invalid texture type: {}", desc_.texture_type);
  return TextureType::kUnknown;
}

auto TextureResource::GetFormat() const noexcept -> Format
{
  if (desc_.format
      >= static_cast<std::underlying_type_t<Format>>(Format::kUnknown)
    && desc_.format
      <= static_cast<std::underlying_type_t<Format>>(Format::kMaxFormat)) {
    return static_cast<Format>(desc_.format);
  }
  LOG_F(WARNING, "Invalid texture format: {}", desc_.format);
  return Format::kUnknown;
}
