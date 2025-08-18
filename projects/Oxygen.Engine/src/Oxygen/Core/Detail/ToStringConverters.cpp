//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <charconv>
#include <cstring>
#include <string>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>

auto oxygen::to_string(oxygen::Format format) -> const char*
{
  switch (format) {
    // clang-format off
    case oxygen::Format::kUnknown:             return "__Unknown__";
    case oxygen::Format::kR8UInt:              return "R8_UINT";
    case oxygen::Format::kR8SInt:              return "R8_SINT";
    case oxygen::Format::kR8UNorm:             return "R8_UNORM";
    case oxygen::Format::kR8SNorm:             return "R8_SNORM";
    case oxygen::Format::kR16UInt:             return "R16_UINT";
    case oxygen::Format::kR16SInt:             return "R16_SINT";
    case oxygen::Format::kR16UNorm:            return "R16_UNORM";
    case oxygen::Format::kR16SNorm:            return "R16_SNORM";
    case oxygen::Format::kR16Float:            return "R16_FLOAT";
    case oxygen::Format::kR32UInt:             return "R32_UINT";
    case oxygen::Format::kR32SInt:             return "R32_SINT";
    case oxygen::Format::kR32Float:            return "R32_FLOAT";
    case oxygen::Format::kRG8UInt:             return "RG8_UINT";
    case oxygen::Format::kRG8SInt:             return "RG8_SINT";
    case oxygen::Format::kRG8UNorm:            return "RG8_UNORM";
    case oxygen::Format::kRG8SNorm:            return "RG8_SNORM";
    case oxygen::Format::kRG16UInt:            return "RG16_UINT";
    case oxygen::Format::kRG16SInt:            return "RG16_SINT";
    case oxygen::Format::kRG16UNorm:           return "RG16_UNORM";
    case oxygen::Format::kRG16SNorm:           return "RG16_SNORM";
    case oxygen::Format::kRG16Float:           return "RG16_FLOAT";
    case oxygen::Format::kRG32UInt:            return "RG32_UINT";
    case oxygen::Format::kRG32SInt:            return "RG32_SINT";
    case oxygen::Format::kRG32Float:           return "RG32_FLOAT";
    case oxygen::Format::kRGB32UInt:           return "RGB32_UINT";
    case oxygen::Format::kRGB32SInt:           return "RGB32_SINT";
    case oxygen::Format::kRGB32Float:          return "RGB32_FLOAT";
    case oxygen::Format::kRGBA8UInt:           return "RGBA8_UINT";
    case oxygen::Format::kRGBA8SInt:           return "RGBA8_SINT";
    case oxygen::Format::kRGBA8UNorm:          return "RGBA8_UNORM";
    case oxygen::Format::kRGBA8UNormSRGB:      return "RGBA8_UNORM_SRGB";
    case oxygen::Format::kRGBA8SNorm:          return "RGBA8_SNORM";
    case oxygen::Format::kBGRA8UNorm:          return "BGRA8_UNORM";
    case oxygen::Format::kBGRA8UNormSRGB:      return "BGRA8_UNORM_SRGB";
    case oxygen::Format::kRGBA16UInt:          return "RGBA16_UINT";
    case oxygen::Format::kRGBA16SInt:          return "RGBA16_SINT";
    case oxygen::Format::kRGBA16UNorm:         return "RGBA16_UNORM";
    case oxygen::Format::kRGBA16SNorm:         return "RGBA16_SNORM";
    case oxygen::Format::kRGBA16Float:         return "RGBA16_FLOAT";
    case oxygen::Format::kRGBA32UInt:          return "RGBA32_UINT";
    case oxygen::Format::kRGBA32SInt:          return "RGBA32_SINT";
    case oxygen::Format::kRGBA32Float:         return "RGBA32_FLOAT";
    case oxygen::Format::kB5G6R5UNorm:         return "B5G6R5_UNORM";
    case oxygen::Format::kB5G5R5A1UNorm:       return "B5G5R5A1_UNORM";
    case oxygen::Format::kB4G4R4A4UNorm:       return "B4G4R4A4_UNORM";
    case oxygen::Format::kR11G11B10Float:      return "R11G11B10_FLOAT";
    case oxygen::Format::kR10G10B10A2UNorm:    return "R10G10B10A2_UNORM";
    case oxygen::Format::kR10G10B10A2UInt:     return "R10G10B10A2_UINT";
    case oxygen::Format::kR9G9B9E5Float:       return "R9G9B9E5_FLOAT";
    case oxygen::Format::kBC1UNorm:            return "BC1_UNORM";
    case oxygen::Format::kBC1UNormSRGB:        return "BC1_UNORM_SRGB";
    case oxygen::Format::kBC2UNorm:            return "BC2_UNORM";
    case oxygen::Format::kBC2UNormSRGB:        return "BC2_UNORM_SRGB";
    case oxygen::Format::kBC3UNorm:            return "BC3_UNORM";
    case oxygen::Format::kBC3UNormSRGB:        return "BC3_UNORM_SRGB";
    case oxygen::Format::kBC4UNorm:            return "BC4_UNORM";
    case oxygen::Format::kBC4SNorm:            return "BC4_SNORM";
    case oxygen::Format::kBC5UNorm:            return "BC5_UNORM";
    case oxygen::Format::kBC5SNorm:            return "BC5_SNORM";
    case oxygen::Format::kBC6HFloatU:          return "BC6H_FLOAT_U";
    case oxygen::Format::kBC6HFloatS:          return "BC6H_FLOAT_S";
    case oxygen::Format::kBC7UNorm:            return "BC7_UNORM";
    case oxygen::Format::kBC7UNormSRGB:        return "BC7_UNORM_SRGB";
    case oxygen::Format::kDepth16:             return "DEPTH16";
    case oxygen::Format::kDepth24Stencil8:     return "DEPTH24_STENCIL8";
    case oxygen::Format::kDepth32:             return "DEPTH32";
    case oxygen::Format::kDepth32Stencil8:     return "DEPTH32_STENCIL8";
    // clang-format on
  }

  return "__NotSupported__";
}

// Helpers used by BindlessHandle to_string implementations
namespace {
inline void append_literal(
  char* dst, size_t& pos, char const* lit, size_t lit_sz)
{
  std::memcpy(dst + pos, lit, lit_sz);
  pos += lit_sz;
}

inline void append_uint(char* dst, size_t& pos, size_t buf_sz, uint32_t v)
{
  auto [p, ec] = std::to_chars(dst + pos, dst + buf_sz, v);
  if (ec == std::errc()) {
    pos = p - dst;
    return;
  }

  // Defensive fallback: if conversion failed (value too large or other
  // reason), write a single '?' and clamp position to avoid overflow.
  if (pos < buf_sz) {
    dst[pos++] = '?';
  }
  if (pos > buf_sz) {
    pos = buf_sz;
  }
}
} // namespace

auto oxygen::to_string(oxygen::BindlessHandle h) -> std::string
{
  char buf[32];
  size_t pos = 0;

  append_literal(buf, pos, "Bindless(i:", sizeof("Bindless(i:") - 1);
  append_uint(buf, pos, sizeof(buf), static_cast<uint32_t>(h.get()));
  append_literal(buf, pos, ")", 1);

  return std::string(buf, pos);
}

auto oxygen::to_string(oxygen::VersionedBindlessHandle const& h) -> std::string
{
  char buf[64];
  size_t pos = 0;

  append_literal(buf, pos, "Bindless(i:", sizeof("Bindless(i:") - 1);
  append_uint(
    buf, pos, sizeof(buf), static_cast<uint32_t>(h.ToBindlessHandle().get()));
  append_literal(buf, pos, ", g:", sizeof(", g:") - 1);
  append_uint(
    buf, pos, sizeof(buf), static_cast<uint32_t>(h.GenerationValue().get()));
  append_literal(buf, pos, ")", 1);

  return std::string(buf, pos);
}

auto oxygen::to_string(const oxygen::ShaderType value) -> const char*
{
  switch (value) {
    // clang-format off
  case ShaderType::kUnknown:         return "__Unknown__";
  case ShaderType::kAmplification:   return "Amplification Shader";
  case ShaderType::kMesh:            return "Mesh Shader";
  case ShaderType::kVertex:          return "Vertex Shader";
  case ShaderType::kHull:            return "Hull Shader";
  case ShaderType::kDomain:          return "Domain Shader";
  case ShaderType::kGeometry:        return "Geometry Shader";
  case ShaderType::kPixel:           return "Pixel Shader";
  case ShaderType::kCompute:         return "Compute Shader";
  case ShaderType::kRayGen:          return "Ray Generation Shader";
  case ShaderType::kIntersection:    return "Intersection Shader";
  case ShaderType::kAnyHit:          return "Any-Hit Shader";
  case ShaderType::kClosestHit:      return "Closest-Hit Shader";
  case ShaderType::kMiss:            return "Miss Shader";
  case ShaderType::kCallable:        return "Callable Shader";
    // clang-format on
  }

  return "__NotSupported__";
}

auto oxygen::to_string(const oxygen::TextureType value) -> const char*
{
  switch (value) {
    // clang-format off
  case TextureType::kUnknown:                    return "__Unknown__";
  case TextureType::kTexture1D:                  return "1D Texture";
  case TextureType::kTexture1DArray:             return "1D Texture Array";
  case TextureType::kTexture2D:                  return "2D Texture";
  case TextureType::kTexture2DArray:             return "2D Texture Array";
  case TextureType::kTextureCube:                return "Cube Texture";
  case TextureType::kTextureCubeArray:           return "Cube Texture Array";
  case TextureType::kTexture2DMultiSample:       return "2D Multi-Sample Texture";
  case TextureType::kTexture2DMultiSampleArray:  return "2D Multi-Sample Texture Array";
  case TextureType::kTexture3D:                  return "3D Texture";
    // clang-format on
  }

  return "__NotSupported__";
}
