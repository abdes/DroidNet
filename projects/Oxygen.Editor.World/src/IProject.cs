// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace Oxygen.Editor.World;

/// <summary>
/// Represents a project within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="IProject"/> interface defines the structure of a project within the Oxygen Editor. It includes properties
/// for accessing project information and the scenes associated with the project.
/// </remarks>
public interface IProject : INotifyPropertyChanging, INotifyPropertyChanged
{
    /// <summary>
    /// Gets the project information.
    /// </summary>
    public IProjectInfo ProjectInfo { get; }

    /// <summary>
    /// Gets the list of scenes associated with the project.
    /// </summary>
    public IList<Scene> Scenes { get; }

    /// <summary>
    /// Gets or sets the active scene for the project.
    /// </summary>
    public Scene? ActiveScene { get; set; }
}
