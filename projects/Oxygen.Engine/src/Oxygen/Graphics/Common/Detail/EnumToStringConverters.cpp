//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <sstream>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
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
