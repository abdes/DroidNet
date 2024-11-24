// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a project within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="IProject"/> interface defines the structure of a project within the Oxygen Editor. It includes properties
/// for accessing project information and the scenes associated with the project.
/// </remarks>
public interface IProject
{
    /// <summary>
    /// Gets the project information.
    /// </summary>
    /// <value>
    /// An object implementing the <see cref="IProjectInfo"/> interface, which contains metadata about the project.
    /// </value>
    public IProjectInfo ProjectInfo { get; }

    /// <summary>
    /// Gets the list of scenes associated with the project.
    /// </summary>
    /// <value>
    /// A list of <see cref="Scene"/> objects representing the scenes within the project.
    /// </value>
    public IList<Scene> Scenes { get; }
}
