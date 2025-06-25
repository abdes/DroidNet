//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>

auto oxygen::graphics::to_string(const Scissors& scissors) -> std::string
{
  return fmt::format("Scissors{{l={}, t={}, r={}, b={}}}", scissors.left,
    scissors.top, scissors.right, scissors.bottom);
}

auto oxygen::graphics::to_string(const NativeObject& obj) -> std::string
{
  if (obj.IsValid()) {
    if (obj.IsPointerHandle()) {
      auto* pointer = obj.AsPointer<void*>();
      // format pointer as 0x00000000
      return fmt::format("NativeObject{{type_id: {}, pointer: {:p}}}",
        obj.OwnerTypeId(), fmt::ptr(pointer));
    }
    if (obj.IsIntegerHandle()) {
      return fmt::format("NativeObject{{type_id: {}, handle: {}}}",
        obj.OwnerTypeId(), obj.AsInteger());
    }
  }
  return "NativeObject{invalid}";
}

auto oxygen::graphics::to_string(const DescriptorHandle& handle) -> std::string
{
  return fmt::format(
    "DescriptorHandle{}{{index: {}, view_type: {}, visibility: {}}}",
    handle.IsValid() ? "" : " (invalid)", handle.GetIndex(),
    nostd::to_string(handle.GetViewType()),
    nostd::to_string(handle.GetVisibility()));
}

auto oxygen::graphics::to_string(const QueueRole value) -> const char*
{
  switch (value) {
  case QueueRole::kGraphics:
    return "Graphics";
  case QueueRole::kCompute:
    return "Compute";
  case QueueRole::kTransfer:
    return "Transfer";
  case QueueRole::kPresent:
    return "Present";
  case QueueRole::kNone:
    return "Unknown";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const QueueAllocationPreference value) -> const
  char*
{
  switch (value) {
  case QueueAllocationPreference::kAllInOne:
    return "AllInOne";
  case QueueAllocationPreference::kDedicated:
    return "Dedicated";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const QueueSharingPreference value) -> const
  char*
{
  switch (value) {
  case QueueSharingPreference::kShared:
    return "Shared";
  case QueueSharingPreference::kSeparate:
    return "Separate";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ShaderType value) -> const char*
{
  switch (value) {
  case ShaderType::kUnknown:
    return "Unknown";
  case ShaderType::kAmplification:
    return "Amplification Shader";
  case ShaderType::kMesh:
    return "Mesh Shader";
  case ShaderType::kVertex:
    return "Vertex Shader";
  case ShaderType::kHull:
    return "Hull Shader";
  case ShaderType::kDomain:
    return "Domain Shader";
  case ShaderType::kGeometry:
    return "Geometry Shader";
  case ShaderType::kPixel:
    return "Pixel Shader";
  case ShaderType::kCompute:
    return "Compute Shader";
  case ShaderType::kRayGen:
    return "Ray Generation Shader";
  case ShaderType::kIntersection:
    return "Intersection Shader";
  case ShaderType::kAnyHit:
    return "Any-Hit Shader";
  case ShaderType::kClosestHit:
    return "Closest-Hit Shader";
  case ShaderType::kMiss:
    return "Miss Shader";
  case ShaderType::kCallable:
    return "Callable Shader";
  case ShaderType::kMaxShaderType:
    return "__Max__";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ResourceStates value) -> std::string
{
  if (value == ResourceStates::kUnknown) {
    return "Unknown";
  }

  std::string result;
  bool first = true;

  // Bitmask to track all checked states
  auto checked_states = ResourceStates::kUnknown;

  // Helper lambda to check if a bit is set and append the state name
  auto check_and_append
    = [&](const ResourceStates state, const char* state_name) {
        if ((value & state) == state) {
          if (!first) {
            result += " | ";
          }
          result += state_name;
          first = false;
          checked_states |= state;
        }
      };

  check_and_append(ResourceStates::kUndefined, "Undefined");
  check_and_append(ResourceStates::kVertexBuffer, "VertexBuffer");
  check_and_append(ResourceStates::kConstantBuffer, "ConstantBuffer");
  check_and_append(ResourceStates::kIndexBuffer, "IndexBuffer");
  check_and_append(ResourceStates::kRenderTarget, "RenderTarget");
  check_and_append(ResourceStates::kUnorderedAccess, "UnorderedAccess");
  check_and_append(ResourceStates::kDepthWrite, "DepthWrite");
  check_and_append(ResourceStates::kDepthRead, "DepthRead");
  check_and_append(ResourceStates::kShaderResource, "ShaderResource");
  check_and_append(ResourceStates::kStreamOut, "StreamOut");
  check_and_append(ResourceStates::kIndirectArgument, "IndirectArgument");
  check_and_append(ResourceStates::kCopyDest, "CopyDest");
  check_and_append(ResourceStates::kCopySource, "CopySource");
  check_and_append(ResourceStates::kResolveDest, "ResolveDest");
  check_and_append(ResourceStates::kResolveSource, "ResolveSource");
  check_and_append(ResourceStates::kInputAttachment, "InputAttachment");
  check_and_append(ResourceStates::kPresent, "Present");
  check_and_append(
    ResourceStates::kBuildAccelStructureRead, "BuildAccelStructureRead");
  check_and_append(
    ResourceStates::kBuildAccelStructureWrite, "BuildAccelStructureWrite");
  check_and_append(ResourceStates::kRayTracing, "RayTracing");
  check_and_append(ResourceStates::kCommon, "Common");
  check_and_append(ResourceStates::kShadingRate, "ShadingRate");
  check_and_append(ResourceStates::kGenericRead, "GenericRead");

  // Validate that all bits in `value` were checked
  DCHECK_EQ_F(checked_states, value,
    "to_string: Unchecked ResourceStates value detected");

  return result;
}

auto oxygen::graphics::to_string(const ShaderStageFlags value) -> std::string
{
  if (value == ShaderStageFlags::kNone) {
    return "None";
  }
  if (value == ShaderStageFlags::kAll) {
    return "All";
  }
  if (value == ShaderStageFlags::kAllGraphics) {
    return "All Graphics";
  }
  if (value == ShaderStageFlags::kAllRayTracing) {
    return "All Ray Tracing";
  }

  std::string result;
  bool first = true;
  auto checked = ShaderStageFlags::kNone;

  auto check_and_append = [&](const ShaderStageFlags flag, const char* name) {
    if ((value & flag) == flag) {
      if (!first) {
        result += "|";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(ShaderStageFlags::kAmplification, "Amplification");
  check_and_append(ShaderStageFlags::kMesh, "Mesh");
  check_and_append(ShaderStageFlags::kVertex, "Vertex");
  check_and_append(ShaderStageFlags::kHull, "Hull");
  check_and_append(ShaderStageFlags::kDomain, "Domain");
  check_and_append(ShaderStageFlags::kGeometry, "Geometry");
  check_and_append(ShaderStageFlags::kPixel, "Pixel");
  check_and_append(ShaderStageFlags::kCompute, "Compute");
  check_and_append(ShaderStageFlags::kRayGen, "RayGen");
  check_and_append(ShaderStageFlags::kIntersection, "Intersection");
  check_and_append(ShaderStageFlags::kAnyHit, "AnyHit");
  check_and_append(ShaderStageFlags::kClosestHit, "ClosestHit");
  check_and_append(ShaderStageFlags::kMiss, "Miss");
  check_and_append(ShaderStageFlags::kCallable, "Callable");

  // Validate that all bits in `value` were checked
  DCHECK_EQ_F(
    checked, value, "to_string: Unchecked ShaderStageFlags value detected");

  return result.empty() ? "__NotSupported__" : result;
}

auto oxygen::graphics::to_string(const ResourceStateTrackingMode value) -> const
  char*
{
  switch (value) {
  case ResourceStateTrackingMode::kDefault:
    return "Default";
  case ResourceStateTrackingMode::kKeepInitialState:
    return "Keep Initial State";
  case ResourceStateTrackingMode::kPermanentState:
    return "Permanent State";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ResourceAccessMode value) -> const char*
{
  switch (value) {
  case ResourceAccessMode::kInvalid:
    return "Invalid";
  case ResourceAccessMode::kImmutable:
    return "Immutable";
  case ResourceAccessMode::kGpuOnly:
    return "GPU Only";
  case ResourceAccessMode::kUpload:
    return "Upload";
  case ResourceAccessMode::kVolatile:
    return "Volatile";
  case ResourceAccessMode::kReadBack:
    return "Read Back";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const TextureDimension value) -> const char*
{
  switch (value) {
  case TextureDimension::kUnknown:
    return "Unknown";
  case TextureDimension::kTexture1DArray:
    return "1D Array";
  case TextureDimension::kTexture1D:
    return "1D";
  case TextureDimension::kTexture2D:
    return "2D";
  case TextureDimension::kTexture2DArray:
    return "2D Array";
  case TextureDimension::kTextureCube:
    return "Cube";
  case TextureDimension::kTextureCubeArray:
    return "Cube Array";
  case TextureDimension::kTexture2DMultiSample:
    return "2D MS";
  case TextureDimension::kTexture2DMultiSampleArray:
    return "2D MS Array";
  case TextureDimension::kTexture3D:
    return "3D";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ResourceViewType value) -> const char*
{
  switch (value) {
  case ResourceViewType::kNone:
    return "None";
  case ResourceViewType::kTexture_SRV:
    return "Texture SRV";
  case ResourceViewType::kTypedBuffer_SRV:
    return "Typed Buffer SRV";
  case ResourceViewType::kStructuredBuffer_SRV:
    return "Structured Buffer SRV";
  case ResourceViewType::kRawBuffer_SRV:
    return "Raw Buffer SRV";
  case ResourceViewType::kConstantBuffer:
    return "Constant Buffer";
  case ResourceViewType::kTexture_UAV:
    return "Texture UAV";
  case ResourceViewType::kTypedBuffer_UAV:
    return "Typed Buffer UAV";
  case ResourceViewType::kStructuredBuffer_UAV:
    return "Structured Buffer UAV";
  case ResourceViewType::kRawBuffer_UAV:
    return "Raw Buffer UAV";
  case ResourceViewType::kSampler:
    return "Sampler";
  case ResourceViewType::kSamplerFeedbackTexture_UAV:
    return "Sampler Feedback Texture UAV";
  case ResourceViewType::kRayTracingAccelStructure:
    return "Ray Tracing Acceleration Structure";
  case ResourceViewType::kTexture_DSV:
    return "Texture DSV";
  case ResourceViewType::kTexture_RTV:
    return "Texture RTV";
  case ResourceViewType::kMaxResourceViewType:
    return "__Max__";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const DescriptorVisibility value) -> const
  char*
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (value) { // NOLINT(clang-diagnostic-switch)
  case DescriptorVisibility::kNone:
    return "None";
  case DescriptorVisibility::kShaderVisible:
    return "ShaderVisible";
  case DescriptorVisibility::kCpuOnly:
    return "CPU-Only";
  }
  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ClearFlags value) -> std::string
{
  if (value == ClearFlags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;

  // Bitmask to track all checked states
  auto checked_states = ClearFlags::kNone;

  // Helper to append flag string if present
  auto check_and_append
    = [&](const ClearFlags flag_to_check, const char* name) {
        if ((value & flag_to_check) == flag_to_check) {
          if (!first) {
            result += "|";
          }
          result += name;
          first = false;
          checked_states |= flag_to_check;
        }
      };

  check_and_append(ClearFlags::kColor, "Color");
  check_and_append(ClearFlags::kDepth, "Depth");
  check_and_append(ClearFlags::kStencil, "Stencil");

  // Validate that all bits in `value` were checked
  DCHECK_EQ_F(
    checked_states, value, "to_string: Unchecked ClearFlags value detected");

  return result;
}

auto oxygen::graphics::to_string(const FillMode mode) -> std::string
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (mode) { // NOLINT(clang-diagnostic-switch)
  case FillMode::kSolid:
    return "Solid";
  case FillMode::kWireFrame:
    return "Wire-frame";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const CullMode value) -> std::string
{
  if (value == CullMode::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;

  // Bitmask to track all checked states (following the ClearFlags pattern)
  auto checked_states = CullMode::kNone;

  auto append_flag = [&](const CullMode flag_to_check, const char* name) {
    if ((value & flag_to_check) == flag_to_check) {
      if (!first) {
        result += "|";
      }
      result += name;
      first = false;
      checked_states |= flag_to_check;
    }
  };

  append_flag(CullMode::kFront, "Front");
  append_flag(CullMode::kBack, "Back");

  DCHECK_EQ_F(
    checked_states, value, "to_string: Unchecked CullMode value detected");

  return result;
}

auto oxygen::graphics::to_string(const CompareOp value) -> std::string
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (value) { // NOLINT(clang-diagnostic-switch)
  case CompareOp::kNever:
    return "Never";
  case CompareOp::kLess:
    return "Less";
  case CompareOp::kEqual:
    return "Equal";
  case CompareOp::kLessOrEqual:
    return "LessEqual";
  case CompareOp::kGreater:
    return "Greater";
  case CompareOp::kNotEqual:
    return "NotEqual";
  case CompareOp::kGreaterOrEqual:
    return "GreaterEqual";
  case CompareOp::kAlways:
    return "Always";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const BlendFactor value) -> std::string
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (value) { // NOLINT(clang-diagnostic-switch)
  case BlendFactor::kZero:
    return "Zero";
  case BlendFactor::kOne:
    return "One";
  case BlendFactor::kSrcColor:
    return "SrcColor";
  case BlendFactor::kInvSrcColor:
    return "InvSrcColor";
  case BlendFactor::kSrcAlpha:
    return "SrcAlpha";
  case BlendFactor::kInvSrcAlpha:
    return "InvSrcAlpha";
  case BlendFactor::kDestColor:
    return "DestColor";
  case BlendFactor::kInvDestColor:
    return "InvDestColor";
  case BlendFactor::kDestAlpha:
    return "DestAlpha";
  case BlendFactor::kInvDestAlpha:
    return "InvDestAlpha";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const BlendOp value) -> std::string
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (value) { // NOLINT(clang-diagnostic-switch)
  case BlendOp::kAdd:
    return "Add";
  case BlendOp::kSubtract:
    return "Subtract";
  case BlendOp::kRevSubtract:
    return "RevSubtract";
  case BlendOp::kMin:
    return "Min";
  case BlendOp::kMax:
    return "Max";
  }

  return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ColorWriteMask value) -> std::string
{
  if (value == ColorWriteMask::kAll) {
    return "All";
  }
  if (value == ColorWriteMask::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;

  // Bitmask to track all checked states (following the ClearFlags pattern)
  auto checked_states = ColorWriteMask::kNone;

  auto append_flag = [&](const ColorWriteMask flag_to_check, const char* name) {
    if ((value & flag_to_check) == flag_to_check) {
      if (!first) {
        result += "|";
      }
      result += name;
      first = false;
      checked_states |= flag_to_check;
    }
  };

  append_flag(ColorWriteMask::kR, "R");
  append_flag(ColorWriteMask::kG, "G");
  append_flag(ColorWriteMask::kB, "B");
  append_flag(ColorWriteMask::kA, "A");

  DCHECK_EQ_F(checked_states, value,
    "to_string: Unchecked ColorWriteMask value detected");

  return result;
}

auto oxygen::graphics::to_string(const PrimitiveType value) -> std::string
{
  // ReSharper disable once CppIncompleteSwitchStatement
  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  switch (value) { // NOLINT(clang-diagnostic-switch)
  case PrimitiveType::kPointList:
    return "PointList";
  case PrimitiveType::kLineList:
    return "LineList";
  case PrimitiveType::kLineStrip:
    return "LineStrip";
  case PrimitiveType::kLineStripWithRestartEnable:
    return "LineStripWithRestart";
  case PrimitiveType::kTriangleList:
    return "TriangleList";
  case PrimitiveType::kTriangleStrip:
    return "TriangleStrip";
  case PrimitiveType::kTriangleStripWithRestartEnable:
    return "TriangleStripWithRestart";
  case PrimitiveType::kPatchList:
    return "PatchList";
  case PrimitiveType::kLineListWithAdjacency:
    return "LineListWithAdjacency";
  case PrimitiveType::kLineStripWithAdjacency:
    return "LineStripWithAdjacency";
  case PrimitiveType::kTriangleListWithAdjacency:
    return "TriangleListWithAdjacency";
  case PrimitiveType::kTriangleStripWithAdjacency:
    return "TriangleStripWithAdjacency";
  }

  return "__NotSupported__";
}
