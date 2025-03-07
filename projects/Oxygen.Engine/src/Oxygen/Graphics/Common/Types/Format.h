//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::graphics {

// ReSharper disable IdentifierTypo
// ReSharper disable CppInconsistentNaming
// ReSharper disable CommentTypo

//! GPU resources data format.
enum class Format : uint8_t {
    kUnknown = 0, //!< Unknown format

    // Single 8-bit values
    kR8UInt = 1, //!< 8-bit unsigned integer
    kR8SInt = 2, //!< 8-bit signed integer
    kR8UNorm = 3, //!< 8-bit unsigned normalized
    kR8SNorm = 4, //!< 8-bit signed normalized

    // Single 16-bit values
    kR16UInt = 5, //!< 16-bit unsigned integer
    kR16SInt = 6, //!< 16-bit signed integer
    kR16UNorm = 7, //!< 16-bit unsigned normalized
    kR16SNorm = 8, //!< 16-bit signed normalized
    kR16Float = 9, //!< 16-bit float

    // Single 32-bit values
    kR32UInt = 10, //!< 32-bit unsigned integer
    kR32SInt = 11, //!< 32-bit signed integer
    kR32Float = 12, //!< 32-bit float

    // Double 8-bit values
    kR8G8UInt = 13, //!< 8-bit unsigned integer (2 components)
    kR8G8SInt = 14, //!< 8-bit signed integer (2 components)
    kR8G8UNorm = 15, //!< 8-bit unsigned normalized (2 components)
    kR8G8SNorm = 16, //!< 8-bit signed normalized (2 components)

    // Double 16-bit values
    kR16G16UInt = 17, //!< 16-bit unsigned integer (2 components)
    kR16G16SInt = 18, //!< 16-bit signed integer (2 components)
    kR16G16UNorm = 19, //!< 16-bit unsigned normalized (2 components)
    kR16G16SNorm = 20, //!< 16-bit signed normalized (2 components)
    kR16G16Float = 21, //!< 16-bit float (2 components)

    // Double 32-bit values
    kR32G32UInt = 22, //!< 32-bit unsigned integer (2 components)
    kR32G32SInt = 23, //!< 32-bit signed integer (2 components)
    kR32G32Float = 24, //!< 32-bit float (2 components)

    // Triple 32-bit values
    kR32G32B32UInt = 25, //!< 32-bit unsigned integer (3 components)
    kR32G32B32SInt = 26, //!< 32-bit signed integer (3 components)
    kR32G32B32Float = 27, //!< 32-bit float (3 components)

    // Quadruple 8-bit values
    kR8G8B8A8UInt = 28, //!< 8-bit unsigned integer (4 components)
    kR8G8B8A8SInt = 29, //!< 8-bit signed integer (4 components)
    kR8G8B8A8UNorm = 30, //!< 8-bit unsigned normalized (4 components)
    kR8G8B8A8UNormSRGB = 31, //!< 8-bit unsigned normalized sRGB (4 components)
    kR8G8B8A8SNorm = 32, //!< 8-bit signed normalized (4 components)
    kB8G8R8A8UNorm = 33, //!< 8-bit unsigned normalized (4 components, BGRA)
    kB8G8R8A8UNormSRGB = 34, //!< 8-bit unsigned normalized sRGB (4 components, BGRA)

    // Quadruple 16-bit values
    kR16G16B16A16UInt = 35, //!< 16-bit unsigned integer (4 components)
    kR16G16B16A16SInt = 36, //!< 16-bit signed integer (4 components)
    kR16G16B16A16UNorm = 37, //!< 16-bit unsigned normalized (4 components)
    kR16G16B16A16SNorm = 38, //!< 16-bit signed normalized (4 components)
    kR16G16B16A16Float = 39, //!< 16-bit float (4 components)

    // Quadruple 32-bit values
    kR32G32B32A32UInt = 40, //!< 32-bit unsigned integer (4 components)
    kR32G32B32A32SInt = 41, //!< 32-bit signed integer (4 components)
    kR32G32B32A32Float = 42, //!< 32-bit float (4 components)

    // Packed types
    kB5G6R5UNorm = 43, //!< 5-6-5 unsigned normalized
    kB5G5R5A1UNorm = 44, //!< 5-5-5-1 unsigned normalized
    kB4G4R4A4UNorm = 45, //!< 4-4-4-4 unsigned normalized
    kR11G11B10Float = 46, //!< 11-11-10 float
    kR10G10B10A2UNorm = 47, //!< 10-10-10-2 unsigned normalized
    kR10G10B10A2UInt = 48, //!< 10-10-10-2 unsigned integer
    kR9G9B9E5Float = 49, //!< 9-9-9-5 float (shared exponent)

    // Block Compressed formats
    kBC1UNorm = 50, //!< BC1 unsigned normalized (aka. DXT1)
    kBC1UNormSRGB = 51, //!< BC1 unsigned normalized sRGB (aka. DXT1)
    kBC2UNorm = 52, //!< BC2 unsigned normalized (aka. DXT3)
    kBC2UNormSRGB = 53, //!< BC2 unsigned normalized sRGB (aka. DXT3)
    kBC3UNorm = 54, //!< BC3 unsigned normalized (aka. DXT5)
    kBC3UNormSRGB = 55, //!< BC3 unsigned normalized sRGB (aka. DXT5)
    kBC4UNorm = 56, //!< BC4 unsigned normalized (aka. RGTC1)
    kBC4SNorm = 57, //!< BC4 signed normalized (aka. RGTC1)
    kBC5UNorm = 58, //!< BC5 unsigned normalized (aka. RGTC2)
    kBC5SNorm = 59, //!< BC5 signed normalized (aka. RGTC2)
    kBC6HFloatU = 60, //!< BC6H unsigned float (aka. BPTC)
    kBC6HFloatS = 61, //!< BC6H signed float (aka. BPTC)
    kBC7UNorm = 62, //!< BC7 unsigned normalized (aka. BPTC)
    kBC7UNormSRGB = 63, //!< BC7 unsigned normalized sRGB (aka. BPTC)

    // Depth formats
    kDepth16 = 64, //!< 16-bit depth buffer
    kDepth24Stencil8 = 65, //!< 24-bit depth buffer + 8-bit stencil buffer
    kDepth32 = 66, //!< 32-bit depth buffer
    kDepth32Stencil8 = 67, //!< 32-bit depth buffer + 8-bit stencil buffer

    kMax = 68 //!< Maximum value for validation
};

} // namespace oxygen::graphics
