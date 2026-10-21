// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
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
    ///     Gets the file system that holds the project's files: the disk for an opened project, any file system for a
    ///     test's. Code that reads a project file reads it through here, so it needs no file system of its own.
    /// </summary>
    public required IFileSystem FileSystem { get; init; }

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

    /// <summary>Gets the pak files the project mounts as read-only cooked libraries.</summary>
    public IReadOnlyList<PakFileMount> PakFileMounts { get; init; } = [];

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
    ///     not replace the context, so <see cref="IProjectSession.Settings"/> holds the current ones.
    /// </summary>
    public ProjectSettings Settings { get; init; } = new();

    /// <summary>
    ///     Creates a context snapshot from a loaded project.
    /// </summary>
    /// <param name="project">The loaded project.</param>
    /// <param name="fileSystem">The file system that holds the project's files.</param>
    /// <returns>The project context.</returns>
    public static ProjectContext FromProject(IProject project, IFileSystem fileSystem)
    {
        ArgumentNullException.ThrowIfNull(project);

        return FromProjectInfo(project.ProjectInfo, fileSystem);
    }

    /// <summary>
    ///     Creates a context snapshot from project metadata.
    /// </summary>
    /// <param name="info">The project metadata.</param>
    /// <param name="fileSystem">The file system that holds the project's files.</param>
    /// <returns>The project context.</returns>
    /// <exception cref="ArgumentException">The project has no location on disk.</exception>
    public static ProjectContext FromProjectInfo(IProjectInfo info, IFileSystem fileSystem)
    {
        ArgumentNullException.ThrowIfNull(info);
        ArgumentNullException.ThrowIfNull(fileSystem);
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
            FileSystem = fileSystem,
            Thumbnail = info.Thumbnail,
            AuthoringMounts = [.. info.AuthoringMounts],
            LocalFolderMounts = [.. info.LocalFolderMounts],
            PakFileMounts = [.. info.PakFileMounts],
            CookedContentOrder = [.. info.CookedContentOrder],
            DefaultSceneId = info.DefaultSceneId,
            Settings = info.Settings,
        };
    }
}
