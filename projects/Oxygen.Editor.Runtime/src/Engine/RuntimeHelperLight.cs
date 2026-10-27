// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>The kind of light a scene helper's handle edits, which names the light's edited property.</summary>
public enum RuntimeHelperLight
{
    /// <summary>No light.</summary>
    None = 0,

    /// <summary>A point light.</summary>
    Point = 1,

    /// <summary>A spot light.</summary>
    Spot = 2,
}
