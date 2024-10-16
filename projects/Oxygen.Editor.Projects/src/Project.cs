// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a game project with scenes.
/// </summary>
/// <param name="info">The metadata information about this project.</param>
public partial class Project(IProjectInfo info) : GameObject, IProject
{
    public IList<Scene> Scenes { get; } = [];

    public IProjectInfo ProjectInfo { get; } = info;
}
