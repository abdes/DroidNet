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
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/Types/ShaderType.h>

auto oxygen::graphics::to_string(const oxygen::graphics::NativeObject& obj) -> std::string
{
    if (obj.IsValid()) {
        try {
            auto* pointer = obj.AsPointer<void*>();
            // format pointer as 0x00000000
            return fmt::format("NativeObject{{type_id: {}, pointer: {:p}}}", static_cast<uint64_t>(obj.OwnerTypeId()), fmt::ptr(pointer));
        } catch (const std::exception&) {
            return fmt::format("NativeObject{{type_id: {}, handle: {}}}", static_cast<uint64_t>(obj.OwnerTypeId()), obj.AsInteger());
        }
    } else {
        return "NativeObject{invalid}";
    }
}

auto oxygen::graphics::to_string(const oxygen::graphics::DescriptorHandle& handle) -> std::string
{
    return fmt::format("DescriptorHandle{}{{index: {}, view_type: {}, visibility: {}}}",
        handle.IsValid() ? "" : " (invalid)",
        handle.GetIndex(), nostd::to_string(handle.GetViewType()), nostd::to_string(handle.GetVisibility()));
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

auto oxygen::graphics::to_string(const QueueAllocationPreference value) -> const char*
{
    switch (value) {
    case QueueAllocationPreference::kAllInOne:
        return "AllInOne";
    case QueueAllocationPreference::kDedicated:
        return "Dedicated";
    }

    return "__NotSupported__";
}

auto oxygen::graphics::to_string(const QueueSharingPreference value) -> const char*
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
    case ShaderType::kVertex:
        return "Vertex Shader";
    case ShaderType::kPixel:
        return "Pixel Shader";
    case ShaderType::kGeometry:
        return "Geometry Shader";
    case ShaderType::kHull:
        return "Hull Shader";
    case ShaderType::kDomain:
        return "Domain Shader";
    case ShaderType::kCompute:
        return "Compute Shader";
    case ShaderType::kAmplification:
        return "Amplification Shader";
    case ShaderType::kMesh:
        return "Mesh Shader";

    case ShaderType::kCount:
        return "__count__";
    }

    return "__NotSupported__";
}

auto oxygen::graphics::to_string(const ResourceStates value) -> std::string
{
    if (value == ResourceStates::kUnknown) {
        return "Unknown";
    }

    std::ostringstream result;
    bool first = true;

    // Bitmask to track all checked states
    auto checked_states = ResourceStates::kUnknown;

    // Helper lambda to check if a bit is set and append the state name
    auto check_and_append = [&](const ResourceStates state, const char* state_name) {
        if ((value & state) == state) {
            if (!first) {
                result << " | ";
            }
            result << state_name;
            first = false;

            // Add the state to the checked_states bitmask
            checked_states |= state;
        }
    };

    // Use the helper lambda to check and append each state
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
    check_and_append(ResourceStates::kBuildAccelStructureRead, "BuildAccelStructureRead");
    check_and_append(ResourceStates::kBuildAccelStructureWrite, "BuildAccelStructureWrite");
    check_and_append(ResourceStates::kRayTracing, "RayTracing");
    check_and_append(ResourceStates::kCommon, "Common");
    check_and_append(ResourceStates::kShadingRate, "ShadingRate");
    check_and_append(ResourceStates::kGenericRead, "GenericRead");

    // Validate that all bits in `value` were checked
    DCHECK_EQ_F(checked_states, value, "to_string: Unchecked ResourceStates value detected");

    // Return the concatenated string
    static std::string result_str;
    result_str = result.str();
    return result_str.c_str();
}

auto oxygen::graphics::to_string(const ResourceStateTrackingMode value) -> const char*
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

auto oxygen::graphics::to_string(ResourceViewType value) -> const char*
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

auto oxygen::graphics::to_string(const DescriptorVisibility value) -> const char*
{
    switch (value) {
    case DescriptorVisibility::kNone:
        return "None";
    case DescriptorVisibility::kShaderVisible:
        return "Shader Visible";
    case DescriptorVisibility::kCpuOnly:
        return "CPU Only";
    case DescriptorVisibility::kMaxDescriptorVisibility:
        return "__Max__";
    }

    return "__NotSupported__";
}

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <sstream>

// Pipeline enums to_string implementations

auto oxygen::graphics::to_string(FillMode mode) -> std::string
{
    switch (mode) {
    case FillMode::kSolid:
        return "Solid";
    case FillMode::kWireframe:
        return "Wireframe";
    }

    return "__Unsupported__";
}

auto oxygen::graphics::to_string(CullMode mode) -> std::string
{
    if (mode == CullMode::kNone) {
        return "None";
    }

    std::ostringstream oss;
    if ((mode & CullMode::kFront) != CullMode::kNone) {
        oss << "Front";
    }
    if ((mode & CullMode::kBack) != CullMode::kNone) {
        if (!oss.str().empty()) {
            oss << "|";
        }
        oss << "Back";
    }

    if (oss.str().empty()) {
        return "__Unsupported__";
    }
    return oss.str();
}

auto oxygen::graphics::to_string(CompareOp op) -> std::string
{
    switch (op) {
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

    return "__Unsupported__";
}

auto oxygen::graphics::to_string(BlendFactor v) -> std::string
{
    switch (v) {
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

    return "__Unsupported__";
}

auto oxygen::graphics::to_string(BlendOp op) -> std::string
{
    switch (op) {
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

    return "__Unsupported__";
}

auto oxygen::graphics::to_string(ColorWriteMask mask) -> std::string
{
    if (mask == ColorWriteMask::kAll) {
        return "All";
    }
    if (mask == ColorWriteMask::kNone) {
        return "None";
    }
    std::ostringstream oss;
    if ((static_cast<uint8_t>(mask) & static_cast<uint8_t>(ColorWriteMask::kR)) != 0) {
        oss << "R";
    }
    if ((static_cast<uint8_t>(mask) & static_cast<uint8_t>(ColorWriteMask::kG)) != 0) {
        oss << "G";
    }
    if ((static_cast<uint8_t>(mask) & static_cast<uint8_t>(ColorWriteMask::kB)) != 0) {
        oss << "B";
    }
    if ((static_cast<uint8_t>(mask) & static_cast<uint8_t>(ColorWriteMask::kA)) != 0) {
        oss << "A";
    }

    if (oss.str().empty()) {
        return "__Unsupported__";
    }
    return oss.str();
}

auto oxygen::graphics::to_string(PrimitiveType t) -> std::string
{
    switch (t) {
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

    return "__Unsupported__";
}
