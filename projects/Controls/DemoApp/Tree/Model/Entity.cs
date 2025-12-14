// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree.Model;

/// <summary>
/// Represents an entity with a name.
/// </summary>
/// <param name="name">The name of the entity.</param>
internal sealed class Entity(string name) : NamedItem(name)
{
    /// <summary>
    /// Gets the collection of child entities of this entity.
    /// </summary>
    public IList<Entity> Entities { get; init; } = [];

    /// <summary>
    /// Gets the light component attached to this entity, if any.
    /// </summary>
    public LightComponent? Light { get; init; }

    /// <summary>
    /// Gets the camera component attached to this entity, if any.
    /// </summary>
    public CameraComponent? Camera { get; init; }

    /// <summary>
    /// Gets the geometry component attached to this entity, if any.
    /// </summary>
    public GeometryComponent? Geometry { get; init; }

    /// <summary>
    /// Gets a value indicating whether this entity has a light component.
    /// </summary>
    public bool HasLight => this.Light is not null;

    /// <summary>
    /// Gets a value indicating whether this entity has a camera component.
    /// </summary>
    public bool HasCamera => this.Camera is not null;

    /// <summary>
    /// Gets a value indicating whether this entity has a geometry component.
    /// </summary>
    public bool HasGeometry => this.Geometry is not null;

    /// <summary>
    /// Clones this entity without copying any child entities.
    /// </summary>
    /// <returns>A new entity instance with the same name and attached components.</returns>
    /// <remarks>
    /// This is intentionally a shallow clone for tree/clipboard usage.
    /// </remarks>
    public Entity CloneWithoutChildren()
        => new(this.Name)
        {
            Light = this.Light is null ? null : new LightComponent(),
            Camera = this.Camera is null ? null : new CameraComponent(),
            Geometry = this.Geometry is null ? null : new GeometryComponent(),
        };
}
