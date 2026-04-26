// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Read-only snapshot of the active project used by editor services.
/// </summary>
public sealed record ProjectContext
{
    /// <summary>
    ///     Gets the project identity.
    /// </summary>
    public required Guid ProjectId { get; init; }

    /// <summary>
    ///     Gets the project display name.
    /// </summary>
    public required string Name { get; init; }

    /// <summary>
    ///     Gets the project category.
    /// </summary>
    public required Category Category { get; init; }

    /// <summary>
    ///     Gets the normalized project root path.
    /// </summary>
    public required string ProjectRoot { get; init; }

    /// <summary>
    ///     Gets the project thumbnail path.
    /// </summary>
    public string? Thumbnail { get; init; }

    /// <summary>
    ///     Gets the project authoring mount roots.
    /// </summary>
    public required IReadOnlyList<ProjectMountPoint> AuthoringMounts { get; init; }

    /// <summary>
    ///     Gets the project local folder mount roots.
    /// </summary>
    public required IReadOnlyList<LocalFolderMount> LocalFolderMounts { get; init; }

    /// <summary>
    ///     Gets the currently known scene metadata.
    /// </summary>
    public required IReadOnlyList<ProjectSceneInfo> Scenes { get; init; }

    /// <summary>
    ///     Creates a context snapshot from a loaded project.
    /// </summary>
    /// <param name="project">The loaded project.</param>
    /// <returns>The project context.</returns>
    public static ProjectContext FromProject(IProject project)
    {
        ArgumentNullException.ThrowIfNull(project);

        var info = project.ProjectInfo;
        return new ProjectContext
        {
            ProjectId = info.Id,
            Name = info.Name,
            Category = info.Category,
            ProjectRoot = info.Location ?? string.Empty,
            Thumbnail = info.Thumbnail,
            AuthoringMounts = [.. info.AuthoringMounts],
            LocalFolderMounts = [.. info.LocalFolderMounts],
            Scenes = [.. project.Scenes.Select(scene => new ProjectSceneInfo(scene.Id, scene.Name))],
        };
    }
}
