// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a game project with scenes.
/// </summary>
/// <param name="info">The metadata information about this project.</param>
public partial class Project(IProjectInfo info) : GameObject, IProject
{
    private Scene? activeScene;

    /// <inheritdoc/>
    public IProjectInfo ProjectInfo { get; } = info;

    /// <inheritdoc/>
    public IList<Scene> Scenes { get; } = [];

    /// <inheritdoc/>
    /// <remarks>
    ///     Only an explicitly accepted editable scene is active. A project with scene metadata but
    ///     no accepted (or a retired) graph reports <see langword="null"/> rather than inferring
    ///     the first scene; initial scene selection belongs to the workspace.
    /// </remarks>
    public Scene? ActiveScene
    {
        get => this.activeScene;
        set
        {
            if (value is not null && !this.Scenes.Contains(value))
            {
                throw new ArgumentException($"scene with name {value} is not part of this project.", nameof(value));
            }

            _ = this.SetProperty(ref this.activeScene, value);
        }
    }
}
