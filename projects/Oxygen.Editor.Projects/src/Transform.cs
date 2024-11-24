// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a transform component of a game entity, specifying its position, rotation, and scale in the scene.
/// </summary>
/// <param name="entity">The owner <see cref="GameEntity" />.</param>
/// <remarks>
/// The <see cref="Transform"/> class represents a transform component of a game entity. It includes properties for the position, rotation, and scale of the entity within the scene. The class provides methods for getting and setting these properties.
/// </remarks>
public partial class Transform(GameEntity entity) : GameComponent(entity)
{
    private Vector3 position;
    private Vector3 rotation;
    private Vector3 scale;

    /// <summary>
    /// Gets or sets the position of the game entity in the scene.
    /// </summary>
    /// <value>
    /// A <see cref="Vector3"/> representing the position of the game entity.
    /// </value>
    public Vector3 Position
    {
        get => this.position;
        set => _ = this.SetField(ref this.position, value);
    }

    /// <summary>
    /// Gets or sets the rotation of the game entity in the scene.
    /// </summary>
    /// <value>
    /// A <see cref="Vector3"/> representing the rotation of the game entity.
    /// </value>
    public Vector3 Rotation
    {
        get => this.rotation;
        set => _ = this.SetField(ref this.rotation, value);
    }

    /// <summary>
    /// Gets or sets the scale of the game entity in the scene.
    /// </summary>
    /// <value>
    /// A <see cref="Vector3"/> representing the scale of the game entity.
    /// </value>
    public Vector3 Scale
    {
        get => this.scale;
        set => _ = this.SetField(ref this.scale, value);
    }
}
