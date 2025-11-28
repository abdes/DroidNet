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
    public Scene? ActiveScene
    {
        get => this.activeScene ?? this.Scenes.FirstOrDefault();
        set
        {
            if (value is not null && !this.Scenes.Contains(value))
            {
                throw new ArgumentException($"scene with name {value} is not part of this project.", nameof(value));
            }

            _ = this.SetProperty(ref this.activeScene, value);
        }
    }

    /// <inheritdoc/>
    // Persistence for projects should be implemented on a concrete DTO-specific API.
    // Leave hydration/dehydration to the concrete classes; GameObject has no persistence helpers.
}
