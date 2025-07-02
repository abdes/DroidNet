//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>

#include <Oxygen/Graphics/Common/Detail/FormatUtils.h>

namespace oxygen::graphics::detail {

// Format mapping table. The rows must be in the exactly same order as Format
// enum members are defined.
static const FormatInfo kFormatInfo[] = {
  // clang-format off
  //    format                 bytes  blk       kind                 red   green  blue   alpha  depth  stencl signed srgb
  { Format::kUnknown,             0,   0, FormatKind::kInteger,     false, false, false, false, false, false, false, false },
  { Format::kR8UInt,              1,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
  { Format::kR8SInt,              1,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
  { Format::kR8UNorm,             1,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
  { Format::kR8SNorm,             1,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
  { Format::kR16UInt,             2,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
  { Format::kR16SInt,             2,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
  { Format::kR16UNorm,            2,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
  { Format::kR16SNorm,            2,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
  { Format::kR16Float,            2,   1, FormatKind::kFloat,       true,  false, false, false, false, false, true,  false },
  { Format::kR32UInt,             4,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
  { Format::kR32SInt,             4,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
  { Format::kR32Float,            4,   1, FormatKind::kFloat,       true,  false, false, false, false, false, true,  false },
  { Format::kRG8UInt,             2,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
  { Format::kRG8SInt,             2,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
  { Format::kRG8UNorm,            2,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
  { Format::kRG8SNorm,            2,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
  { Format::kRG16UInt,            4,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
  { Format::kRG16SInt,            4,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
  { Format::kRG16UNorm,           4,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
  { Format::kRG16SNorm,           4,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
  { Format::kRG16Float,           4,   1, FormatKind::kFloat,       true,  true,  false, false, false, false, true,  false },
  { Format::kRG32UInt,            8,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
  { Format::kRG32SInt,            8,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
  { Format::kRG32Float,           8,   1, FormatKind::kFloat,       true,  true,  false, false, false, false, true,  false },
  { Format::kRGB32UInt,          12,   1, FormatKind::kInteger,     true,  true,  true,  false, false, false, false, false },
  { Format::kRGB32SInt,          12,   1, FormatKind::kInteger,     true,  true,  true,  false, false, false, true,  false },
  { Format::kRGB32Float,         12,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, true,  false },
  { Format::kRGBA8UInt,           4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
  { Format::kRGBA8SInt,           4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
  { Format::kRGBA8UNorm,          4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kRGBA8UNormSRGB,      4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kRGBA8SNorm,          4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, true,  false },
  { Format::kBGRA8UNorm,          4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kBGRA8UNormSRGB,      4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kRGBA16UInt,          8,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
  { Format::kRGBA16SInt,          8,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
  { Format::kRGBA16UNorm,         8,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kRGBA16SNorm,         8,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, true,  false },
  { Format::kRGBA16Float,         8,   1, FormatKind::kFloat,       true,  true,  true,  true,  false, false, true,  false },
  { Format::kRGBA32UInt,         16,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
  { Format::kRGBA32SInt,         16,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
  { Format::kRGBA32Float,        16,   1, FormatKind::kFloat,       true,  true,  true,  true,  false, false, true,  false },
  { Format::kB5G6R5UNorm,         2,   1, FormatKind::kNormalized,  true,  true,  true,  false, false, false, false, false },
  { Format::kB5G5R5A1UNorm,       2,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kB4G4R4A4UNorm,       2,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kR11G11B10Float,      4,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
  { Format::kR10G10B10A2UNorm,    4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kR10G10B10A2UInt,     4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
  { Format::kR9G9B9E5Float,       4,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
  { Format::kBC1UNorm,            8,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kBC1UNormSRGB,        8,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kBC2UNorm,           16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kBC2UNormSRGB,       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kBC3UNorm,           16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kBC3UNormSRGB,       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kBC4UNorm,            8,   4, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
  { Format::kBC4SNorm,            8,   4, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
  { Format::kBC5UNorm,           16,   4, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
  { Format::kBC5SNorm,           16,   4, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
  { Format::kBC6HFloatU,         16,   4, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
  { Format::kBC6HFloatS,         16,   4, FormatKind::kFloat,       true,  true,  true,  false, false, false, true,  false },
  { Format::kBC7UNorm,           16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
  { Format::kBC7UNormSRGB,       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
  { Format::kDepth16,             2,   1, FormatKind::kDepthStencil,false, false, false, false, true,  false, false, false },
  { Format::kDepth24Stencil8,     4,   1, FormatKind::kDepthStencil,false, false, false, false, true,  true,  false, false },
  { Format::kDepth32,             4,   1, FormatKind::kDepthStencil,false, false, false, false, true,  false, false, false },
  { Format::kDepth32Stencil8,     8,   1, FormatKind::kDepthStencil,false, false, false, false, true,  true,  false, false },
  // clang-format on
};

auto GetFormatInfo(Format format) -> const FormatInfo&
{
  static_assert(sizeof(kFormatInfo) / sizeof(FormatInfo)
      == static_cast<size_t>(Format::kMax),
    "The format info table doesn't have the right number of elements");

  if (static_cast<uint32_t>(format) >= static_cast<uint32_t>(Format::kMax)) {
    return kFormatInfo[0]; // kUnknown
  }

  const FormatInfo& info = kFormatInfo[static_cast<uint32_t>(format)];
  assert(info.format == format);
  return info;
}

} // namespace oxygen::graphics::detail
