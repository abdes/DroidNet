// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Model;

/// <summary>
/// Represents a scene with a name and a collection of entities.
/// </summary>
/// <param name="name">The name of the scene.</param>
public class Scene(string name) : NamedItem(name)
{
    /// <summary>
    /// Gets the collection of entities in the scene.
    /// </summary>
    public IList<Entity> Entities { get; init; } = [];
}
