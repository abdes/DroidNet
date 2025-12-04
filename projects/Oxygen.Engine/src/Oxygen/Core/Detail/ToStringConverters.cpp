//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <fmt/format.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>

// ReSharper disable StringLiteralTypo

auto oxygen::to_string(const Format value) -> const char*
{
  switch (value) {
    // clang-format off
    case Format::kUnknown:             return "__Unknown__";
    case Format::kR8UInt:              return "R8_UINT";
    case Format::kR8SInt:              return "R8_SINT";
    case Format::kR8UNorm:             return "R8_UNORM";
    case Format::kR8SNorm:             return "R8_SNORM";
    case Format::kR16UInt:             return "R16_UINT";
    case Format::kR16SInt:             return "R16_SINT";
    case Format::kR16UNorm:            return "R16_UNORM";
    case Format::kR16SNorm:            return "R16_SNORM";
    case Format::kR16Float:            return "R16_FLOAT";
    case Format::kR32UInt:             return "R32_UINT";
    case Format::kR32SInt:             return "R32_SINT";
    case Format::kR32Float:            return "R32_FLOAT";
    case Format::kRG8UInt:             return "RG8_UINT";
    case Format::kRG8SInt:             return "RG8_SINT";
    case Format::kRG8UNorm:            return "RG8_UNORM";
    case Format::kRG8SNorm:            return "RG8_SNORM";
    case Format::kRG16UInt:            return "RG16_UINT";
    case Format::kRG16SInt:            return "RG16_SINT";
    case Format::kRG16UNorm:           return "RG16_UNORM";
    case Format::kRG16SNorm:           return "RG16_SNORM";
    case Format::kRG16Float:           return "RG16_FLOAT";
    case Format::kRG32UInt:            return "RG32_UINT";
    case Format::kRG32SInt:            return "RG32_SINT";
    case Format::kRG32Float:           return "RG32_FLOAT";
    case Format::kRGB32UInt:           return "RGB32_UINT";
    case Format::kRGB32SInt:           return "RGB32_SINT";
    case Format::kRGB32Float:          return "RGB32_FLOAT";
    case Format::kRGBA8UInt:           return "RGBA8_UINT";
    case Format::kRGBA8SInt:           return "RGBA8_SINT";
    case Format::kRGBA8UNorm:          return "RGBA8_UNORM";
    case Format::kRGBA8UNormSRGB:      return "RGBA8_UNORM_SRGB";
    case Format::kRGBA8SNorm:          return "RGBA8_SNORM";
    case Format::kBGRA8UNorm:          return "BGRA8_UNORM";
    case Format::kBGRA8UNormSRGB:      return "BGRA8_UNORM_SRGB";
    case Format::kRGBA16UInt:          return "RGBA16_UINT";
    case Format::kRGBA16SInt:          return "RGBA16_SINT";
    case Format::kRGBA16UNorm:         return "RGBA16_UNORM";
    case Format::kRGBA16SNorm:         return "RGBA16_SNORM";
    case Format::kRGBA16Float:         return "RGBA16_FLOAT";
    case Format::kRGBA32UInt:          return "RGBA32_UINT";
    case Format::kRGBA32SInt:          return "RGBA32_SINT";
    case Format::kRGBA32Float:         return "RGBA32_FLOAT";
    case Format::kB5G6R5UNorm:         return "B5G6R5_UNORM";
    case Format::kB5G5R5A1UNorm:       return "B5G5R5A1_UNORM";
    case Format::kB4G4R4A4UNorm:       return "B4G4R4A4_UNORM";
    case Format::kR11G11B10Float:      return "R11G11B10_FLOAT";
    case Format::kR10G10B10A2UNorm:    return "R10G10B10A2_UNORM";
    case Format::kR10G10B10A2UInt:     return "R10G10B10A2_UINT";
    case Format::kR9G9B9E5Float:       return "R9G9B9E5_FLOAT";
    case Format::kBC1UNorm:            return "BC1_UNORM";
    case Format::kBC1UNormSRGB:        return "BC1_UNORM_SRGB";
    case Format::kBC2UNorm:            return "BC2_UNORM";
    case Format::kBC2UNormSRGB:        return "BC2_UNORM_SRGB";
    case Format::kBC3UNorm:            return "BC3_UNORM";
    case Format::kBC3UNormSRGB:        return "BC3_UNORM_SRGB";
    case Format::kBC4UNorm:            return "BC4_UNORM";
    case Format::kBC4SNorm:            return "BC4_SNORM";
    case Format::kBC5UNorm:            return "BC5_UNORM";
    case Format::kBC5SNorm:            return "BC5_SNORM";
    case Format::kBC6HFloatU:          return "BC6H_FLOAT_U";
    case Format::kBC6HFloatS:          return "BC6H_FLOAT_S";
    case Format::kBC7UNorm:            return "BC7_UNORM";
    case Format::kBC7UNormSRGB:        return "BC7_UNORM_SRGB";
    case Format::kDepth16:             return "DEPTH16";
    case Format::kDepth24Stencil8:     return "DEPTH24_STENCIL8";
    case Format::kDepth32:             return "DEPTH32";
    case Format::kDepth32Stencil8:     return "DEPTH32_STENCIL8";
    // clang-format on
  }

  return "__NotSupported__";
}

// Format helpers replaced with fmt::format usages for clarity and safety.
auto oxygen::to_string(BindlessHeapIndex h) -> std::string
{
  return fmt::format("BindlessHeapIndex(i:{})", h.get());
}

auto oxygen::to_string(ShaderVisibleIndex idx) -> std::string
{
  return fmt::format("ShaderVisibleIndex(i:{})", idx.get());
}

auto oxygen::to_string(const VersionedBindlessHandle& h) -> std::string
{
  return fmt::format("VersionedBindlessHandle(i:{}, g:{})",
    h.ToBindlessHandle().get(), h.GenerationValue().get());
}

auto oxygen::to_string(FrameSlotNumber s) -> std::string
{
  using namespace oxygen::frame;

  if (s == kInvalidSlot) {
    return fmt::format("Frame(slot:__Invalid__)");
  }
#if !defined(NDEBUG)
  // Validate slot range: valid slots are [0, kFramesInFlight)
  if (s.get() >= kFramesInFlight.get()) {
    return fmt::format("Frame(slot:{}-OOB)", s.get());
  }
#endif
  return fmt::format("Frame(slot:{})", s.get());
}

auto oxygen::to_string(FrameSequenceNumber seq) -> std::string
{
  using namespace oxygen::frame;

  // Validate sequence number: valid sequences are [0, kMaxSequenceNumber)
  if (seq == kInvalidSequenceNumber) {
    return fmt::format("Frame(seq:__Invalid__)");
  }
  return fmt::format("Frame(seq:{})", seq.get());
}

auto oxygen::to_string(BindlessItemCount count) -> std::string
{
  return std::to_string(count.get());
}

auto oxygen::to_string(FrameSlotCount sc) -> std::string
{
  return std::to_string(sc.get());
}

auto oxygen::to_string(BindlessHeapCapacity capacity) -> std::string
{
  return std::to_string(capacity.get());
}

auto oxygen::to_string(VersionedBindlessHandle::Generation gen) -> std::string
{
  return std::to_string(gen.get());
}

auto oxygen::to_string(const ShaderType value) -> const char*
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

auto oxygen::to_string(const TextureType value) -> const char*
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

auto oxygen::to_string(const ViewPort& viewport) -> std::string
{
  return fmt::format(
    "ViewPort{{tl.x={}, tl.y={}, w={}, h={}, min_depth={}, max_depth={}}}",
    viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height,
    viewport.min_depth, viewport.max_depth);
}

auto oxygen::to_string(const Scissors& scissors) -> std::string
{
  return fmt::format("Scissors{{l={}, t={}, r={}, b={}}}", scissors.left,
    scissors.top, scissors.right, scissors.bottom);
}

// Format helpers replaced with fmt::format usages for clarity and safety.
auto oxygen::to_string(const ViewId& v) -> std::string
{
  return fmt::format("ViewId({})", v.get());
}
