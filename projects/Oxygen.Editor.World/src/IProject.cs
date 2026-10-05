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
    /// Gets or sets the scene whose editable graph is accepted for editing.
    /// </summary>
    /// <value>
    ///     The accepted scene, or <see langword="null"/> when no scene graph is accepted, including
    ///     after the accepted graph was retired.
    /// </value>
    public Scene? ActiveScene { get; set; }
}
