// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace Oxygen.Editor.World;

/// <summary>
/// Represents a project within the Oxygen Editor.
/// </summary>
/// <remarks>
/// The <see cref="IProject"/> interface defines the structure of a project within the Oxygen Editor. A project's scenes
/// are its scene files.
/// </remarks>
public interface IProject : INotifyPropertyChanging, INotifyPropertyChanged
{
    /// <summary>
    /// Gets the project information.
    /// </summary>
    public IProjectInfo ProjectInfo { get; }
}
