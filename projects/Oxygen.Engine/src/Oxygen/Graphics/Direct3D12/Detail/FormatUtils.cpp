//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>

#include <Oxygen/Graphics/Common/Types/Format.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>

namespace oxygen::graphics::d3d12::detail {

// Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
static const DxgiFormatMapping kDxgiFormatMappings[] = {
    // clang-format off
    // generic_format              resource_format                     srv_format                             rtv_format
    { Format::kUnknown,            DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                   DXGI_FORMAT_UNKNOWN                    },
    // Single 8-bit values
    { Format::kR8UInt,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UINT,                   DXGI_FORMAT_R8_UINT                    },
    { Format::kR8SInt,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SINT,                   DXGI_FORMAT_R8_SINT                    },
    { Format::kR8UNorm,            DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UNORM,                  DXGI_FORMAT_R8_UNORM                   },
    { Format::kR8SNorm,            DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SNORM,                  DXGI_FORMAT_R8_SNORM                   },
    // Single 16-bit values
    { Format::kR16UInt,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UINT,                  DXGI_FORMAT_R16_UINT                   },
    { Format::kR16SInt,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SINT,                  DXGI_FORMAT_R16_SINT                   },
    { Format::kR16UNorm,           DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                 DXGI_FORMAT_R16_UNORM                  },
    { Format::kR16SNorm,           DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SNORM,                 DXGI_FORMAT_R16_SNORM                  },
    { Format::kR16Float,           DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_FLOAT,                 DXGI_FORMAT_R16_FLOAT                  },
    // Single 32-bit values
    { Format::kR32UInt,            DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_UINT,                  DXGI_FORMAT_R32_UINT                   },
    { Format::kR32SInt,            DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_SINT,                  DXGI_FORMAT_R32_SINT                   },
    { Format::kR32Float,           DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                 DXGI_FORMAT_R32_FLOAT                  },
    // Double 8-bit values
    { Format::kRG8UInt,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UINT,                 DXGI_FORMAT_R8G8_UINT                  },
    { Format::kRG8SInt,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SINT,                 DXGI_FORMAT_R8G8_SINT                  },
    { Format::kRG8UNorm,           DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UNORM,                DXGI_FORMAT_R8G8_UNORM                 },
    { Format::kRG8SNorm,           DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SNORM,                DXGI_FORMAT_R8G8_SNORM                 },
    // Double 16-bit values
    { Format::kRG16UInt,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UINT,               DXGI_FORMAT_R16G16_UINT                },
    { Format::kRG16SInt,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SINT,               DXGI_FORMAT_R16G16_SINT                },
    { Format::kRG16UNorm,          DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UNORM,              DXGI_FORMAT_R16G16_UNORM               },
    { Format::kRG16SNorm,          DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SNORM,              DXGI_FORMAT_R16G16_SNORM               },
    { Format::kRG16Float,          DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_FLOAT,              DXGI_FORMAT_R16G16_FLOAT               },
    // Double 32-bit values
    { Format::kRG32UInt,           DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_UINT,               DXGI_FORMAT_R32G32_UINT                },
    { Format::kRG32SInt,           DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_SINT,               DXGI_FORMAT_R32G32_SINT                },
    { Format::kRG32Float,          DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_FLOAT,              DXGI_FORMAT_R32G32_FLOAT               },
    // Triple 32-bit values
    { Format::kRGB32UInt,          DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_UINT,            DXGI_FORMAT_R32G32B32_UINT             },
    { Format::kRGB32SInt,          DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_SINT,            DXGI_FORMAT_R32G32B32_SINT             },
    { Format::kRGB32Float,         DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_FLOAT,           DXGI_FORMAT_R32G32B32_FLOAT            },
    // Quadruple 8-bit values
    { Format::kRGBA8UInt,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UINT,             DXGI_FORMAT_R8G8B8A8_UINT              },
    { Format::kRGBA8SInt,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SINT,             DXGI_FORMAT_R8G8B8A8_SINT              },
    { Format::kRGBA8UNorm,         DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM,            DXGI_FORMAT_R8G8B8A8_UNORM             },
    { Format::kRGBA8UNormSRGB,     DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,       DXGI_FORMAT_R8G8B8A8_UNORM_SRGB        },
    { Format::kRGBA8SNorm,         DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SNORM,            DXGI_FORMAT_R8G8B8A8_SNORM             },
    { Format::kBGRA8UNorm,         DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM,            DXGI_FORMAT_B8G8R8A8_UNORM             },
    { Format::kBGRA8UNormSRGB,     DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,       DXGI_FORMAT_B8G8R8A8_UNORM_SRGB        },
    // Quadruple 16-bit values
    { Format::kRGBA16UInt,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UINT,         DXGI_FORMAT_R16G16B16A16_UINT          },
    { Format::kRGBA16SInt,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SINT,         DXGI_FORMAT_R16G16B16A16_SINT          },
    { Format::kRGBA16UNorm,        DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UNORM,        DXGI_FORMAT_R16G16B16A16_UNORM         },
    { Format::kRGBA16SNorm,        DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SNORM,        DXGI_FORMAT_R16G16B16A16_SNORM         },
    { Format::kRGBA16Float,        DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_FLOAT,        DXGI_FORMAT_R16G16B16A16_FLOAT         },
    // Quadruple 32-bit values
    { Format::kRGBA32UInt,         DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_UINT,         DXGI_FORMAT_R32G32B32A32_UINT          },
    { Format::kRGBA32SInt,         DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_SINT,         DXGI_FORMAT_R32G32B32A32_SINT          },
    { Format::kRGBA32Float,        DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_FLOAT,        DXGI_FORMAT_R32G32B32A32_FLOAT         },
    // Packed types
    { Format::kB5G6R5UNorm,        DXGI_FORMAT_B5G6R5_UNORM,           DXGI_FORMAT_B5G6R5_UNORM,              DXGI_FORMAT_B5G6R5_UNORM               },
    { Format::kB5G5R5A1UNorm,      DXGI_FORMAT_B5G5R5A1_UNORM,         DXGI_FORMAT_B5G5R5A1_UNORM,            DXGI_FORMAT_B5G5R5A1_UNORM             },
    { Format::kB4G4R4A4UNorm,      DXGI_FORMAT_B4G4R4A4_UNORM,         DXGI_FORMAT_B4G4R4A4_UNORM,            DXGI_FORMAT_B4G4R4A4_UNORM             },
    { Format::kR11G11B10Float,     DXGI_FORMAT_R11G11B10_FLOAT,        DXGI_FORMAT_R11G11B10_FLOAT,           DXGI_FORMAT_R11G11B10_FLOAT            },
    { Format::kR10G10B10A2UNorm,   DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UNORM,         DXGI_FORMAT_R10G10B10A2_UNORM          },
    { Format::kR10G10B10A2UInt,    DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UINT,          DXGI_FORMAT_R10G10B10A2_UINT           },
    { Format::kR9G9B9E5Float,      DXGI_FORMAT_R9G9B9E5_SHAREDEXP,     DXGI_FORMAT_R9G9B9E5_SHAREDEXP,        DXGI_FORMAT_R9G9B9E5_SHAREDEXP         },
    // Block Compressed formats
    { Format::kBC1UNorm,           DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM,                 DXGI_FORMAT_BC1_UNORM                  },
    { Format::kBC1UNormSRGB,       DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM_SRGB,            DXGI_FORMAT_BC1_UNORM_SRGB             },
    { Format::kBC2UNorm,           DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM,                 DXGI_FORMAT_BC2_UNORM                  },
    { Format::kBC2UNormSRGB,       DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM_SRGB,            DXGI_FORMAT_BC2_UNORM_SRGB             },
    { Format::kBC3UNorm,           DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM,                 DXGI_FORMAT_BC3_UNORM                  },
    { Format::kBC3UNormSRGB,       DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM_SRGB,            DXGI_FORMAT_BC3_UNORM_SRGB             },
    { Format::kBC4UNorm,           DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_UNORM,                 DXGI_FORMAT_BC4_UNORM                  },
    { Format::kBC4SNorm,           DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_SNORM,                 DXGI_FORMAT_BC4_SNORM                  },
    { Format::kBC5UNorm,           DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_UNORM,                 DXGI_FORMAT_BC5_UNORM                  },
    { Format::kBC5SNorm,           DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_SNORM,                 DXGI_FORMAT_BC5_SNORM                  },
    { Format::kBC6HFloatU,         DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_UF16,                 DXGI_FORMAT_BC6H_UF16                  },
    { Format::kBC6HFloatS,         DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_SF16,                 DXGI_FORMAT_BC6H_SF16                  },
    { Format::kBC7UNorm,           DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM,                 DXGI_FORMAT_BC7_UNORM                  },
    { Format::kBC7UNormSRGB,       DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM_SRGB,            DXGI_FORMAT_BC7_UNORM_SRGB             },
    // Depth formats
    { Format::kDepth16,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,                 DXGI_FORMAT_D16_UNORM                  },
    { Format::kDepth24Stencil8,    DXGI_FORMAT_R24G8_TYPELESS,         DXGI_FORMAT_R24_UNORM_X8_TYPELESS,     DXGI_FORMAT_D24_UNORM_S8_UINT          },
    { Format::kDepth32,            DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,                 DXGI_FORMAT_D32_FLOAT                  },
    { Format::kDepth32Stencil8,    DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,  DXGI_FORMAT_D32_FLOAT_S8X24_UINT       },
    // clang-format on
};

} // namespace oxygen::graphics::d3d12::detail

auto oxygen::graphics::d3d12::detail::GetDxgiFormatMapping(Format generic_format)
    -> const DxgiFormatMapping&
{
    static_assert(sizeof(kDxgiFormatMappings) / sizeof(DxgiFormatMapping) == static_cast<size_t>(Format::kMax),
        "The DXGI format mapping table doesn't have the right number of elements");

    if (static_cast<uint32_t>(generic_format) >= static_cast<uint32_t>(Format::kMax)) {
        // Return kUnknown for invalid format
        return kDxgiFormatMappings[static_cast<uint32_t>(Format::kUnknown)];
    }

    const DxgiFormatMapping& mapping = kDxgiFormatMappings[static_cast<uint32_t>(generic_format)];
    // Ensure the mapping in the table corresponds to the requested format
    assert(mapping.generic_format == generic_format);
    return mapping;
}
