// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

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
    ///     Gets the project folder on disk.
    /// </summary>
    public required FileSystemPath ProjectRoot { get; init; }

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

    /// <summary>Gets the saved source order, from lowest to highest priority.</summary>
    public IReadOnlyList<CookedContentSource> CookedContentOrder { get; init; } = [];

    /// <summary>
    ///     Gets a value indicating whether workspace activation should open an initial scene.
    /// </summary>
    public bool OpenInitialScene { get; init; } = true;

    /// <summary>
    ///     Gets the scene requested by project activation, when a template or workflow selected one.
    /// </summary>
    public AssetPath? InitialScene { get; init; }

    /// <summary>
    ///     Gets the project's configured default scene identity, or <see langword="null"/> when none is configured.
    /// </summary>
    public SceneId? DefaultSceneId { get; init; }

    /// <summary>
    ///     Gets the project's settings as its manifest held them when this context was created. A settings change does
    ///     not activate the project again, so <see cref="IProjectContextService.Settings"/> holds the current ones.
    /// </summary>
    public ProjectSettings Settings { get; init; } = new();

    /// <summary>
    ///     Creates a context snapshot from a loaded project.
    /// </summary>
    /// <param name="project">The loaded project.</param>
    /// <returns>The project context.</returns>
    public static ProjectContext FromProject(IProject project)
    {
        ArgumentNullException.ThrowIfNull(project);

        return FromProjectInfo(project.ProjectInfo);
    }

    /// <summary>
    ///     Creates a context snapshot from project metadata.
    /// </summary>
    /// <param name="info">The project metadata.</param>
    /// <returns>The project context.</returns>
    /// <exception cref="ArgumentException">The project has no location on disk.</exception>
    public static ProjectContext FromProjectInfo(IProjectInfo info)
    {
        ArgumentNullException.ThrowIfNull(info);
        if (string.IsNullOrWhiteSpace(info.Location))
        {
            throw new ArgumentException($"Project '{info.Name}' has no location on disk.", nameof(info));
        }

        return new ProjectContext
        {
            ProjectId = info.Id,
            Name = info.Name,
            Category = info.Category,
            ProjectRoot = FileSystemPath.From(info.Location),
            Thumbnail = info.Thumbnail,
            AuthoringMounts = [.. info.AuthoringMounts],
            LocalFolderMounts = [.. info.LocalFolderMounts],
            CookedContentOrder = [.. info.CookedContentOrder],
            DefaultSceneId = info.DefaultSceneId,
            Settings = info.Settings,
        };
    }
}
