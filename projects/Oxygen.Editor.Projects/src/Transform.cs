// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Numerics;

/// <summary>
/// Represents a transform component of a game entity, specifying its position, rotation and scale in the scene.
/// </summary>
/// <param name="entity">The owner <see cref="GameEntity" />.</param>
public partial class Transform(GameEntity entity) : GameComponent(entity)
{
    private Vector3 position;
    private Vector3 rotation;
    private Vector3 scale;

    public Vector3 Position
    {
        get => this.position;
        set => _ = this.SetField(ref this.position, value);
    }

    public Vector3 Rotation
    {
        get => this.rotation;
        set => _ = this.SetField(ref this.rotation, value);
    }

    public Vector3 Scale
    {
        get => this.scale;
        set => _ = this.SetField(ref this.scale, value);
    }
}
