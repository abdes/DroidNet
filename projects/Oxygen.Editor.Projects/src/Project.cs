// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a game project.
/// </summary>
/// <param name="info">The metadata information about this project.</param>
public sealed class Project(IProjectInfo info) : IProject
{
    /// <inheritdoc/>
    public IProjectInfo ProjectInfo { get; } = info;
}
