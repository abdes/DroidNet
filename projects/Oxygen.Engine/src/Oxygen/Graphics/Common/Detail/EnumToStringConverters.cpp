//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceState.h>
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

auto oxygen::graphics::to_string(const ResourceState value) -> const char*
{
    switch (value) {
    case ResourceState::kUnknown:
        return "Unknown";
    case ResourceState::kUndefined:
        return "Undefined";
    case ResourceState::kVertexBuffer:
        return "VertexBuffer";
    case ResourceState::kConstantBuffer:
        return "ConstantBuffer";
    case ResourceState::kIndexBuffer:
        return "IndexBuffer";
    case ResourceState::kRenderTarget:
        return "RenderTarget";
    case ResourceState::kUnorderedAccess:
        return "UnorderedAccess";
    case ResourceState::kDepthWrite:
        return "DepthWrite";
    case ResourceState::kDepthRead:
        return "DepthRead";
    case ResourceState::kShaderResource:
        return "ShaderResource";
    case ResourceState::kStreamOut:
        return "StreamOut";
    case ResourceState::kIndirectArgument:
        return "IndirectArgument";
    case ResourceState::kCopyDest:
        return "CopyDest";
    case ResourceState::kCopySource:
        return "CopySource";
    case ResourceState::kResolveDest:
        return "ResolveDest";
    case ResourceState::kResolveSource:
        return "ResolveSource";
    case ResourceState::kInputAttachment:
        return "InputAttachment";
    case ResourceState::kPresent:
        return "Present";
    case ResourceState::kBuildAccelStructureRead:
        return "BuildAccelStructureRead";
    case ResourceState::kBuildAccelStructureWrite:
        return "BuildAccelStructureWrite";
    case ResourceState::kRayTracing:
        return "RayTracing";
    case ResourceState::kCommon:
        return "Common";
    case ResourceState::kShadingRate:
        return "ShadingRate";
    case ResourceState::kGenericRead:
        return "GenericRead";
    }

    return "__NotSupported__";
}
