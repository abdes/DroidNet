// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The viewport tool that decides which gizmo the selection shows.</summary>
public enum RuntimeTransformTool
{
    /// <summary>Selection only; no gizmo.</summary>
    Select = 0,

    /// <summary>Move along axes, planes or the view plane.</summary>
    Translate = 1,

    /// <summary>Turn about an axis or the view direction.</summary>
    Rotate = 2,

    /// <summary>Scale along the active node's axes, or uniformly.</summary>
    Scale = 3,
}
