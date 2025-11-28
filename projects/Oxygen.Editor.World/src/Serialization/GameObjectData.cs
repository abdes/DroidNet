// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Data transfer object for game objects (entities with both name and ID).
/// </summary>
public record GameObjectData : NamedData
{
    /// <summary>
    /// Gets or initializes the unique identifier of the game object.
    /// </summary>
    public required Guid Id { get; init; }
}
