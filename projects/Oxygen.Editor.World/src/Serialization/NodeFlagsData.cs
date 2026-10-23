// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for a node's authored node flags.
/// </summary>
/// <remarks>
/// The visibility and shadow flags store their source modes; an absent one takes the scene schema's default. The modes
/// a new node starts with depend on whether it is a root, which its creator decides.
/// </remarks>
public record NodeFlagsData : ComponentData
{
    /// <summary>
    /// Gets or sets the source mode of the node's scene visibility.
    /// </summary>
    public VisibilitySource Visibility { get; set; } = SceneSchema.Node.Defaults.Visibility;

    /// <summary>
    /// Gets or sets the source mode of the node's geometry shadow casting.
    /// </summary>
    public ShadowSource CastsShadows { get; set; } = SceneSchema.Node.Defaults.CastsShadows;

    /// <summary>
    /// Gets or sets the source mode of the node's geometry shadow receiving.
    /// </summary>
    public ShadowSource ReceivesShadows { get; set; } = SceneSchema.Node.Defaults.ReceivesShadows;

    /// <summary>
    /// Gets or sets a value indicating whether the node is static.
    /// </summary>
    public bool IsStatic { get; set; } = SceneSchema.Node.Defaults.IsStatic;

    /// <summary>
    /// Gets or sets a value indicating whether ray casting can select the node.
    /// </summary>
    public bool IsRayCastingSelectable { get; set; } = SceneSchema.Node.Defaults.IsRayCastingSelectable;

    /// <summary>
    /// Gets or sets a value indicating whether the node ignores its parent's transform.
    /// </summary>
    public bool IgnoreParentTransform { get; set; } = SceneSchema.Node.Defaults.IgnoreParentTransform;

    /// <summary>
    /// Gets these flags with the source modes a new node starts with: the scene schema's root defaults for a root, its
    /// defaults for a child.
    /// </summary>
    /// <param name="isRoot">Whether the new node is a root.</param>
    /// <returns>The flags with their creation modes.</returns>
    public NodeFlagsData WithCreationModes(bool isRoot)
        => isRoot
            ? this with
            {
                Visibility = SceneSchema.Node.RootDefaults.Visibility,
                CastsShadows = SceneSchema.Node.RootDefaults.CastsShadows,
                ReceivesShadows = SceneSchema.Node.RootDefaults.ReceivesShadows,
            }
            : this with
            {
                Visibility = SceneSchema.Node.Defaults.Visibility,
                CastsShadows = SceneSchema.Node.Defaults.CastsShadows,
                ReceivesShadows = SceneSchema.Node.Defaults.ReceivesShadows,
            };
}
