// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a node's authored node flags.
/// </summary>
/// <remarks>
/// The visibility and shadow flags store their source modes; an absent one is Inherit. The modes a new node starts
/// with depend on whether it is a root, which its creator decides.
/// </remarks>
public record NodeFlagsData : ComponentData
{
    /// <summary>
    /// Gets the source mode of the node's scene visibility.
    /// </summary>
    public VisibilitySource Visibility { get; init; }

    /// <summary>
    /// Gets the source mode of the node's geometry shadow casting.
    /// </summary>
    public ShadowSource CastsShadows { get; init; }

    /// <summary>
    /// Gets the source mode of the node's geometry shadow receiving.
    /// </summary>
    public ShadowSource ReceivesShadows { get; init; }

    /// <summary>
    /// Gets a value indicating whether the node is static.
    /// </summary>
    public bool IsStatic { get; init; }

    /// <summary>
    /// Gets or sets a value indicating whether ray casting can select the node.
    /// </summary>
    public bool IsRayCastingSelectable { get; set; } = true;

    /// <summary>
    /// Gets a value indicating whether the node ignores its parent's transform.
    /// </summary>
    public bool IgnoreParentTransform { get; init; }

    /// <summary>
    /// Gets these flags with the source modes a new node starts with: Shown, On and On for a root, Inherit for all three
    /// for a child.
    /// </summary>
    /// <param name="isRoot">Whether the new node is a root.</param>
    /// <returns>The flags with their creation modes.</returns>
    public NodeFlagsData WithCreationModes(bool isRoot)
        => isRoot
            ? this with { Visibility = VisibilitySource.Shown, CastsShadows = ShadowSource.On, ReceivesShadows = ShadowSource.On }
            : this with { Visibility = VisibilitySource.Inherit, CastsShadows = ShadowSource.Inherit, ReceivesShadows = ShadowSource.Inherit };
}
