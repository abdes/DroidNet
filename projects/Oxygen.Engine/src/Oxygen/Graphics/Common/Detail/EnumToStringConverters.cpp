//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/Types/ShaderType.h>

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

auto oxygen::graphics::to_string(const ResourceStates value) -> const char*
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

auto oxygen::graphics::to_string(ResourceAccessMode value) -> const char*
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

auto oxygen::graphics::to_string(TextureDimension value) -> const char*
{
    switch (value) {
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
    case TextureDimension::kTexture2DMS:
        return "2D MS";
    case TextureDimension::kTexture2DMSArray:
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

auto oxygen::graphics::to_string(DescriptorVisibility value) -> const char*
{
    switch (value) {
    case DescriptorVisibility::kShaderVisible:
        return "Shader Visible";
    case DescriptorVisibility::kCpuOnly:
        return "CPU Only";
    }

    return "__NotSupported__";
}
