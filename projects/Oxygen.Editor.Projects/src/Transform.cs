// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Represents a transform component of a scene node, specifying its position, rotation, and scale in the scene.
/// </summary>
/// <param name="node">The owner <see cref="SceneNode" />.</param>
/// <remarks>
///     The <see cref="Transform" /> class represents a transform component of a scene node. It includes properties for
///     the position, rotation, and scale of the scene node within the scene. The class provides methods for getting and
///     setting these properties.
/// </remarks>
public partial class Transform(SceneNode node) : GameComponent(node)
{
    private Vector3 position;
    private Vector3 rotation;
    private Vector3 scale;

    /// <summary>
    ///     Gets or sets the position of the scene node in the scene.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the position of the scene node.
    /// </value>
    public Vector3 Position
    {
        get => this.position;
        set => _ = this.SetField(ref this.position, value);
    }

    /// <summary>
    ///     Gets or sets the rotation of the scene node in the scene.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the rotation of the scene node.
    /// </value>
    public Vector3 Rotation
    {
        get => this.rotation;
        set => _ = this.SetField(ref this.rotation, value);
    }

    /// <summary>
    ///     Gets or sets the scale of the scene node in the scene.
    /// </summary>
    /// <value>
    ///     A <see cref="Vector3" /> representing the scale of the scene node.
    /// </value>
    public Vector3 Scale
    {
        get => this.scale;
        set => _ = this.SetField(ref this.scale, value);
    }
}
