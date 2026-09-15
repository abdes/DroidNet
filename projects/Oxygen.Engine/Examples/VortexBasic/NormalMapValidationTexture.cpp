//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "VortexBasic/NormalMapValidationTexture.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImporter.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::examples::vortex_basic {

struct NormalMapValidationTexture::LoadState {
  std::atomic<bool> complete { false };
  std::shared_ptr<data::TextureResource> texture;
};

NormalMapValidationTexture::NormalMapValidationTexture(
  const observer_ptr<content::IAssetLoader> loader)
  : loader_(loader)
  , load_state_(std::make_shared<LoadState>())
{
  namespace ci = content::import;
  CHECK_NOTNULL_F(loader_.get());

  // Positive tangent Y is essential: losing the bitangent sign under an X
  // reflection turns the expected upward normal into a downward one.
  auto image = ci::ScratchImage::CreateFromData(1U, 1U, Format::kRGBA8UNorm, 4U,
    { std::byte { 128 }, std::byte { 204 }, std::byte { 230 },
      std::byte { 255 } });
  auto desc = ci::TextureImportDesc {};
  desc.source_id = "VortexBasic/NormalMapValidation";
  desc.width = 1U;
  desc.height = 1U;
  desc.intent = ci::TextureIntent::kNormalTS;
  desc.source_color_space = ColorSpace::kLinear;
  desc.mip_policy = ci::MipPolicy::kNone;
  desc.output_format = Format::kRGBA8UNorm;
  auto cooked = ci::CookScratchImage(
    std::move(image), desc, ci::D3D12PackingPolicy::Instance());
  CHECK_F(cooked.has_value(), "Failed to cook validation normal map");
  const auto& payload = cooked->payload;

  using TextureDesc = data::pak::core::TextureResourceDesc;
  auto packed_desc = TextureDesc {};
  packed_desc.data_offset = sizeof(TextureDesc);
  packed_desc.size_bytes
    = static_cast<data::pak::core::DataBlobSizeT>(payload.payload.size());
  packed_desc.texture_type
    = static_cast<std::uint8_t>(payload.desc.texture_type);
  packed_desc.width = payload.desc.width;
  packed_desc.height = payload.desc.height;
  packed_desc.depth = payload.desc.depth;
  packed_desc.array_layers = payload.desc.array_layers;
  packed_desc.mip_levels = payload.desc.mip_levels;
  packed_desc.format = static_cast<std::uint8_t>(payload.desc.format);
  packed_desc.alignment = 256U;

  auto packed = std::make_shared<std::vector<std::uint8_t>>(
    sizeof(TextureDesc) + payload.payload.size());
  std::memcpy(packed->data(), &packed_desc, sizeof(TextureDesc));
  std::memcpy(packed->data() + sizeof(TextureDesc), payload.payload.data(),
    payload.payload.size());
  key_ = loader_->MintSyntheticTextureKey();
  loader_->StartLoadTexture(
    content::CookedResourceData<data::TextureResource> {
      .key = key_, .bytes = std::span<const std::uint8_t>(*packed) },
    [state = load_state_, packed](
      std::shared_ptr<data::TextureResource> texture) {
      // The loader's input span remains valid until this callback completes.
      static_cast<void>(packed);
      state->texture = std::move(texture);
      state->complete.store(true, std::memory_order_release);
    });
}

NormalMapValidationTexture::~NormalMapValidationTexture()
{
  if (pinned_) {
    static_cast<void>(loader_->UnpinResource(key_));
  }
}

auto NormalMapValidationTexture::EnsureReady() -> bool
{
  if (!load_state_->complete.load(std::memory_order_acquire)) {
    return false;
  }
  CHECK_F(load_state_->texture != nullptr, "Validation normal-map load failed");
  if (!pinned_) {
    pinned_ = loader_->PinResource(key_);
    CHECK_F(pinned_, "Could not pin validation normal map");
    LOG_F(INFO,
      "Validation normal map ready: tangent normal approximately (0, 0.6, "
      "0.8)");
  }
  return true;
}

} // namespace oxygen::examples::vortex_basic
