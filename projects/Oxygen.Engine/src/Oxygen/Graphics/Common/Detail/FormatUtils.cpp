//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>

#include "FormatUtils.h"
#include <Oxygen/Graphics/Common/Detail/FormatUtils.h>

namespace oxygen::graphics::detail {

// Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
static const FormatInfo kFormatInfo[] = {
    // clang-format off
    //    format                   name                  bytes  blk       kind                 red   green  blue   alpha  depth  stencl signed srgb
    { Format::kUnknown,            "UNKNOWN",               0,   0, FormatKind::kInteger,     false, false, false, false, false, false, false, false },
    { Format::kR8UInt,             "R8_UINT",               1,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
    { Format::kR8SInt,             "R8_SINT",               1,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
    { Format::kR8UNorm,            "R8_UNORM",              1,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
    { Format::kR8SNorm,            "R8_SNORM",              1,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
    { Format::kR16UInt,            "R16_UINT",              2,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
    { Format::kR16SInt,            "R16_SINT",              2,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
    { Format::kR16UNorm,           "R16_UNORM",             2,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
    { Format::kR16SNorm,           "R16_SNORM",             2,   1, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
    { Format::kR16Float,           "R16_FLOAT",             2,   1, FormatKind::kFloat,       true,  false, false, false, false, false, true,  false },
    { Format::kR32UInt,            "R32_UINT",              4,   1, FormatKind::kInteger,     true,  false, false, false, false, false, false, false },
    { Format::kR32SInt,            "R32_SINT",              4,   1, FormatKind::kInteger,     true,  false, false, false, false, false, true,  false },
    { Format::kR32Float,           "R32_FLOAT",             4,   1, FormatKind::kFloat,       true,  false, false, false, false, false, true,  false },
    { Format::kR8G8UInt,           "RG8_UINT",              2,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
    { Format::kR8G8SInt,           "RG8_SINT",              2,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
    { Format::kR8G8UNorm,          "RG8_UNORM",             2,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
    { Format::kR8G8SNorm,          "RG8_SNORM",             2,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
    { Format::kR16G16UInt,         "RG16_UINT",             4,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
    { Format::kR16G16SInt,         "RG16_SINT",             4,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
    { Format::kR16G16UNorm,        "RG16_UNORM",            4,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
    { Format::kR16G16SNorm,        "RG16_SNORM",            4,   1, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
    { Format::kR16G16Float,        "RG16_FLOAT",            4,   1, FormatKind::kFloat,       true,  true,  false, false, false, false, true,  false },
    { Format::kR32G32UInt,         "RG32_UINT",             8,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, false, false },
    { Format::kR32G32SInt,         "RG32_SINT",             8,   1, FormatKind::kInteger,     true,  true,  false, false, false, false, true,  false },
    { Format::kR32G32Float,        "RG32_FLOAT",            8,   1, FormatKind::kFloat,       true,  true,  false, false, false, false, true,  false },
    { Format::kR32G32B32UInt,      "RGB32_UINT",           12,   1, FormatKind::kInteger,     true,  true,  true,  false, false, false, false, false },
    { Format::kR32G32B32SInt,      "RGB32_SINT",           12,   1, FormatKind::kInteger,     true,  true,  true,  false, false, false, true,  false },
    { Format::kR32G32B32Float,     "RGB32_FLOAT",          12,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, true,  false },
    { Format::kR8G8B8A8UInt,       "RGBA8_UINT",            4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
    { Format::kR8G8B8A8SInt,       "RGBA8_SINT",            4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
    { Format::kR8G8B8A8UNorm,      "RGBA8_UNORM",           4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kR8G8B8A8UNormSRGB,  "SRGBA8_UNORM",          4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kR8G8B8A8SNorm,      "RGBA8_SNORM",           4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, true,  false },
    { Format::kB8G8R8A8UNorm,      "BGRA8_UNORM",           4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kB8G8R8A8UNormSRGB,  "SBGRA8_UNORM",          4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kR16G16B16A16UInt,   "RGBA16_UINT",           8,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
    { Format::kR16G16B16A16SInt,   "RGBA16_SINT",           8,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
    { Format::kR16G16B16A16UNorm,  "RGBA16_UNORM",          8,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kR16G16B16A16SNorm,  "RGBA16_SNORM",          8,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, true,  false },
    { Format::kR16G16B16A16Float,  "RGBA16_FLOAT",          8,   1, FormatKind::kFloat,       true,  true,  true,  true,  false, false, true,  false },
    { Format::kR32G32B32A32UInt,   "RGBA32_UINT",          16,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
    { Format::kR32G32B32A32SInt,   "RGBA32_SINT",          16,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, true,  false },
    { Format::kR32G32B32A32Float,  "RGBA32_FLOAT",         16,   1, FormatKind::kFloat,       true,  true,  true,  true,  false, false, true,  false },
    { Format::kB5G6R5UNorm,        "B5G6R5_UNORM",          2,   1, FormatKind::kNormalized,  true,  true,  true,  false, false, false, false, false },
    { Format::kB5G5R5A1UNorm,      "B5G5R5A1_UNORM",        2,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kB4G4R4A4UNorm,      "BGRA4_UNORM",           2,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kR11G11B10Float,     "R11G11B10_FLOAT",       4,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
    { Format::kR10G10B10A2UNorm,   "R10G10B10A2_UNORM",     4,   1, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kR10G10B10A2UInt,    "R10G10B10A2_UINT",      4,   1, FormatKind::kInteger,     true,  true,  true,  true,  false, false, false, false },
    { Format::kR9G9B9E5Float,      "R9G9B9E5_FLOAT",        4,   1, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
    { Format::kBC1UNorm,           "BC1_UNORM",             8,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kBC1UNormSRGB,       "BC1_UNORM_SRGB",        8,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kBC2UNorm,           "BC2_UNORM",            16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kBC2UNormSRGB,       "BC2_UNORM_SRGB",       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kBC3UNorm,           "BC3_UNORM",            16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kBC3UNormSRGB,       "BC3_UNORM_SRGB",       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kBC4UNorm,           "BC4_UNORM",             8,   4, FormatKind::kNormalized,  true,  false, false, false, false, false, false, false },
    { Format::kBC4SNorm,           "BC4_SNORM",             8,   4, FormatKind::kNormalized,  true,  false, false, false, false, false, true,  false },
    { Format::kBC5UNorm,           "BC5_UNORM",            16,   4, FormatKind::kNormalized,  true,  true,  false, false, false, false, false, false },
    { Format::kBC5SNorm,           "BC5_SNORM",            16,   4, FormatKind::kNormalized,  true,  true,  false, false, false, false, true,  false },
    { Format::kBC6HFloatU,         "BC6H_UFLOAT",          16,   4, FormatKind::kFloat,       true,  true,  true,  false, false, false, false, false },
    { Format::kBC6HFloatS,         "BC6H_SFLOAT",          16,   4, FormatKind::kFloat,       true,  true,  true,  false, false, false, true,  false },
    { Format::kBC7UNorm,           "BC7_UNORM",            16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, false },
    { Format::kBC7UNormSRGB,       "BC7_UNORM_SRGB",       16,   4, FormatKind::kNormalized,  true,  true,  true,  true,  false, false, false, true  },
    { Format::kDepth16,            "D16",                   2,   1, FormatKind::kDepthStencil,false, false, false, false, true,  false, false, false },
    { Format::kDepth24Stencil8,    "D24S8",                 4,   1, FormatKind::kDepthStencil,false, false, false, false, true,  true,  false, false },
    { Format::kDepth32,            "D32",                   4,   1, FormatKind::kDepthStencil,false, false, false, false, true,  false, false, false },
    { Format::kDepth32Stencil8,    "D32S8",                 8,   1, FormatKind::kDepthStencil,false, false, false, false, true,  true,  false, false },
    // clang-format on
};

auto GetFormatInfo(Format format) -> const FormatInfo&
{
    static_assert(sizeof(kFormatInfo) / sizeof(FormatInfo) == static_cast<size_t>(Format::kMax),
        "The format info table doesn't have the right number of elements");

    if (static_cast<uint32_t>(format) >= static_cast<uint32_t>(Format::kMax)) {
        return kFormatInfo[0]; // kUnknown
    }

    const FormatInfo& info = kFormatInfo[static_cast<uint32_t>(format)];
    assert(info.format == format);
    return info;
}

auto to_string(Format value) -> const char*
{
    return detail::GetFormatInfo(value).name;
}

} // namespace oxygen::graphics::detail
