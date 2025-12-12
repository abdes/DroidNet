// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree.Model;

/// <summary>
/// Represents a project with a name and a collection of scenes.
/// </summary>
/// <param name="name">The name of the project.</param>
internal sealed class Project(string name) : NamedItem(name)
{
    /// <summary>
    /// Gets the collection of scenes in the project.
    /// </summary>
    public IList<Scene> Scenes { get; } = [];
}
