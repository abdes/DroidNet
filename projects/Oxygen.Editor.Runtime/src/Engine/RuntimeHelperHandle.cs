// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>A scene helper part the pointer can drag to edit a light.</summary>
public enum RuntimeHelperHandle
{
    /// <summary>No handle.</summary>
    None = 0,

    /// <summary>A point or spot light's range.</summary>
    Range = 1,

    /// <summary>A spot light's inner cone half angle.</summary>
    InnerCone = 2,

    /// <summary>A spot light's outer cone half angle.</summary>
    OuterCone = 3,
}
