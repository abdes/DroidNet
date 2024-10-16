// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json.Serialization;

/// <summary>
/// Represents a component of a game entity, such as transform, geometry, material, etc.
/// </summary>
/// <param name="entity">The owner <see cref="GameEntity" />.</param>
[JsonDerivedType(typeof(Transform), typeDiscriminator: "Transform")]
[JsonDerivedType(typeof(GameComponent), typeDiscriminator: "Base")]
public partial class GameComponent(GameEntity entity) : GameObject
{
    [JsonIgnore]
    public GameEntity Entity { get; } = entity;
}
