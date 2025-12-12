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
}
