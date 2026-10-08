// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The axes a translate or rotate gizmo follows; scale always follows the active node's axes.</summary>
public enum RuntimeTransformSpace
{
    /// <summary>The world axes.</summary>
    World = 0,

    /// <summary>The active node's axes.</summary>
    Local = 1,
}
