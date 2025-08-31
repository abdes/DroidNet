//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Headless/Commands/ClearFramebufferCommand.h>
#include <Oxygen/Graphics/Headless/Texture.h>

namespace oxygen::graphics::headless {

ClearFramebufferCommand::ClearFramebufferCommand(const Framebuffer* fb,
  std::optional<std::vector<std::optional<Color>>> color_clear_values,
  std::optional<float> depth_clear_value,
  std::optional<uint8_t> stencil_clear_value)
  : framebuffer_(fb)
  , color_clear_values_(std::move(color_clear_values))
  , depth_clear_value_(depth_clear_value)
  , stencil_clear_value_(stencil_clear_value)
{
}

auto ClearFramebufferCommand::DoExecute(CommandContext& /*ctx*/) -> void
{
  if (!framebuffer_) {
    LOG_F(WARNING, "ClearFramebufferCommand: no framebuffer");
    return;
  }

  // Iterate color attachments from the framebuffer descriptor and simulate
  // clears. Framebuffer stores attachments in its descriptor.
  const auto& desc = framebuffer_->GetDescriptor();
  const auto& color_attachments = desc.color_attachments;
  for (auto i = 0u; i < color_attachments.size(); ++i) {
    const auto& att = color_attachments[i];
    if (!att.IsValid()) {
      continue;
    }
    auto tex_shared = att.texture;
    if (!tex_shared) {
      continue;
    }
    auto tex = static_cast<Texture*>(tex_shared.get());
    if (!tex) {
      LOG_F(WARNING,
        "ClearFramebufferCommand: attachment {} is not headless-backed", i);
      continue;
    }

    Color clear_color {};
    if (color_clear_values_ && i < color_clear_values_->size()
      && (*color_clear_values_)[i]) {
      clear_color = *(*color_clear_values_)[i];
    } else {
      const auto& tdesc = tex->GetDescriptor();
      if (tdesc.use_clear_value) {
        clear_color = tdesc.clear_value;
      } else {
        clear_color = Color {};
      }
    }

    // Perform a simple RGBA8 clear into the texture backing using the
    // headless WriteBacking helper. We assume base mip level and tightly
    // packed rows for this simple emulation.
    const auto& tdesc = tex->GetDescriptor();
    constexpr uint32_t bpp = 4u;
    const auto row_bytes = tdesc.width * bpp;
    const auto image_bytes
      = row_bytes * (tdesc.height * std::max<uint32_t>(1u, tdesc.array_size));

    // Build a single row of pixels
    std::vector<uint8_t> row(row_bytes);
    for (auto x = 0u; x < tdesc.width; ++x) {
      const auto idx = static_cast<size_t>(x) * 4u;
      row[idx + 0] = static_cast<uint8_t>(clear_color.r * 255.0f);
      row[idx + 1] = static_cast<uint8_t>(clear_color.g * 255.0f);
      row[idx + 2] = static_cast<uint8_t>(clear_color.b * 255.0f);
      row[idx + 3] = static_cast<uint8_t>(clear_color.a * 255.0f);
    }

    // Fill the texture by writing rows sequentially.
    std::vector<uint8_t> image;
    image.reserve(image_bytes);
    for (auto y = 0u;
      y < (tdesc.height * std::max<uint32_t>(1u, tdesc.array_size)); ++y) {
      image.insert(image.end(), row.begin(), row.end());
    }

    // Write into texture backing (clamped to available space by WriteBacking).
    tex->WriteBacking(image.data(), 0, static_cast<uint32_t>(image.size()));
    LOG_F(INFO, "Headless: cleared attachment {} ({} bytes)", i, image.size());
  }

  // Depth/stencil clearing not implemented per-texel here; log and return.
  if (depth_clear_value_ || stencil_clear_value_) {
    LOG_F(INFO,
      "Headless: simulated depth/stencil clear (depth set? {}, stencil set? "
      "{})",
      depth_clear_value_.has_value(), stencil_clear_value_.has_value());
  }
}

auto ClearFramebufferCommand::Serialize(std::ostream& os) const -> void
{
  os << "ClearFramebufferCommand\n";
}

} // namespace oxygen::graphics::headless
