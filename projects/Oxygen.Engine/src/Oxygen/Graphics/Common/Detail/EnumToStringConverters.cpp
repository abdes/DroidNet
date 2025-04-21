//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Types/Queues.h>
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
    return "Unknown";
}

auto oxygen::graphics::to_string(QueueAllocationPreference value) -> const char*
{
    switch (value) {
    case QueueAllocationPreference::kAllInOne:
        return "AllInOne";
    case QueueAllocationPreference::kDedicated:
        return "Dedicated";
    }
    return "Unknown";
}

auto oxygen::graphics::to_string(QueueSharingPreference value) -> const char*
{
    switch (value) {
    case QueueSharingPreference::kShared:
        return "Shared";
    case QueueSharingPreference::kSeparate:
        return "Separate";
    }
    return "Unknown";
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
    return "unknown";
}
