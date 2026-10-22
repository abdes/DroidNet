// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a node's authored node flags.
/// </summary>
public record NodeFlagsData : ComponentData
{
    /// <summary>
    /// Gets a value indicating whether the node is visible.
    /// </summary>
    public bool IsVisible { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the node's geometry casts shadows.
    /// </summary>
    public bool CastsShadows { get; init; }

    /// <summary>
    /// Gets a value indicating whether the node's geometry receives shadows.
    /// </summary>
    public bool ReceivesShadows { get; init; }

    /// <summary>
    /// Gets a value indicating whether the node is static.
    /// </summary>
    public bool IsStatic { get; init; }

    /// <summary>
    /// Gets a value indicating whether ray casting can select the node.
    /// </summary>
    public bool IsRayCastingSelectable { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the node ignores its parent's transform.
    /// </summary>
    public bool IgnoreParentTransform { get; init; }
}
