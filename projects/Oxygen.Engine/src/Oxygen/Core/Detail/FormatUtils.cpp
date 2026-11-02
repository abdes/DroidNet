//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::graphics::detail {

// Format mapping table. The rows must be in the exactly same order as Format
// enum members are defined.
static constexpr auto kFormatInfo = std::to_array<FormatInfo>({
  // clang-format off
  //    format                 bytes  blk       kind                 red   green  blue   alpha  depth  stencl signed srgb
  { .format=Format::kUnknown,             .bytes_per_block=0,   .block_size=0, .kind=FormatKind::kInteger,     .has_red=false, .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR8UInt,              .bytes_per_block=1,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR8SInt,              .bytes_per_block=1,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR8UNorm,             .bytes_per_block=1,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR8SNorm,             .bytes_per_block=1,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR16UInt,             .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR16SInt,             .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR16UNorm,            .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR16SNorm,            .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR16Float,            .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR32UInt,             .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR32SInt,             .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kR32Float,            .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG8UInt,             .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRG8SInt,             .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG8UNorm,            .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRG8SNorm,            .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG16UInt,            .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRG16SInt,            .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG16UNorm,           .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRG16SNorm,           .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG16Float,           .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG32UInt,            .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRG32SInt,            .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRG32Float,           .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGB32UInt,          .bytes_per_block=12,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGB32SInt,          .bytes_per_block=12,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGB32Float,         .bytes_per_block=12,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA8UInt,           .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGBA8SInt,           .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA8UNorm,          .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGBA8UNormSRGB,      .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kRGBA8SNorm,          .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kBGRA8UNorm,          .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBGRA8UNormSRGB,      .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kRGBA16UInt,          .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGBA16SInt,          .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA16UNorm,         .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGBA16SNorm,         .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA16Float,         .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA32UInt,         .bytes_per_block=16,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kRGBA32SInt,         .bytes_per_block=16,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kRGBA32Float,        .bytes_per_block=16,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kB5G6R5UNorm,         .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kB5G5R5A1UNorm,       .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kB4G4R4A4UNorm,       .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR11G11B10Float,      .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR10G10B10A2UNorm,    .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR10G10B10A2UInt,     .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kInteger,     .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kR9G9B9E5Float,       .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC1UNorm,            .bytes_per_block=8,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC1UNormSRGB,        .bytes_per_block=8,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kBC2UNorm,           .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC2UNormSRGB,       .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kBC3UNorm,           .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC3UNormSRGB,       .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kBC4UNorm,            .bytes_per_block=8,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC4SNorm,            .bytes_per_block=8,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kBC5UNorm,           .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC5SNorm,           .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=false, .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kBC6HFloatU,         .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC6HFloatS,         .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kFloat,       .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=false, .has_depth=false, .has_stencil=false, .is_signed=true,  .is_srgb=false },
  { .format=Format::kBC7UNorm,           .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kBC7UNormSRGB,       .bytes_per_block=16,   .block_size=4, .kind=FormatKind::kNormalized,  .has_red=true,  .has_green=true,  .has_blue=true,  .has_alpha=true,  .has_depth=false, .has_stencil=false, .is_signed=false, .is_srgb=true  },
  { .format=Format::kDepth16,             .bytes_per_block=2,   .block_size=1, .kind=FormatKind::kDepthStencil,.has_red=false, .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=true,  .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kDepth24Stencil8,     .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kDepthStencil,.has_red=false, .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=true,  .has_stencil=true,  .is_signed=false, .is_srgb=false },
  { .format=Format::kDepth32,             .bytes_per_block=4,   .block_size=1, .kind=FormatKind::kDepthStencil,.has_red=false, .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=true,  .has_stencil=false, .is_signed=false, .is_srgb=false },
  { .format=Format::kDepth32Stencil8,     .bytes_per_block=8,   .block_size=1, .kind=FormatKind::kDepthStencil,.has_red=false, .has_green=false, .has_blue=false, .has_alpha=false, .has_depth=true,  .has_stencil=true,  .is_signed=false, .is_srgb=false },
  // clang-format on
});

auto GetFormatInfo(Format format) -> const FormatInfo&
{
  static_assert(
    kFormatInfo.size() == static_cast<size_t>(Format::kMaxFormat) + 1,
    "The format info table doesn't have the right number of elements");

  if (static_cast<uint32_t>(format)
    > static_cast<uint32_t>(Format::kMaxFormat)) {
    return kFormatInfo.front(); // kUnknown
  }

  const FormatInfo& info = kFormatInfo.at(static_cast<uint32_t>(format));
  assert(info.format == format);
  return info;
}

auto RowBytes(const Format format, const uint32_t width) -> uint64_t
{
  const auto& info = GetFormatInfo(format);
  if (info.block_size == 0U) {
    return 0U;
  }
  const auto blocks
    = (uint64_t { width } + info.block_size - 1U) / info.block_size;
  return blocks * info.bytes_per_block;
}

auto BlockRows(const Format format, const uint32_t height) -> uint32_t
{
  const auto& info = GetFormatInfo(format);
  if (info.block_size == 0U) {
    return 0U;
  }
  return static_cast<uint32_t>(
    (uint64_t { height } + info.block_size - 1U) / info.block_size);
}

auto IsHdr(Format format) -> bool
{
  const auto& info = GetFormatInfo(format);
  return info.kind == FormatKind::kFloat && !info.is_srgb;
}

} // namespace oxygen::graphics::detail
