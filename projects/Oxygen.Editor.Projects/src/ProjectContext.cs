// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Read-only snapshot of the active project used by editor services.
/// </summary>
public sealed record ProjectContext : IFileReferenceLocator
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
    ///     Gets the resolved mount table: every declared or locally added mount, with its location on this machine and
    ///     where that came from. It is the one table every component maps mount paths through.
    /// </summary>
    public required IReadOnlyList<ResolvedMount> Mounts { get; init; }

    /// <summary>Gets what resolving the mount table found: unbound and missing mounts, and refused declarations.</summary>
    public IReadOnlyList<MountFinding> MountFindings { get; init; } = [];

    /// <summary>Gets the source mounts that are bound: the folders the project authors, cooks and watches.</summary>
    public IEnumerable<ResolvedMount> SourceMounts
        => this.Mounts.Where(static mount => mount is { Kind: MountKind.Source, IsBound: true });

    /// <summary>
    ///     Gets the folders that hold the files the project edits: the project root, and the folder of each bound source
    ///     mount outside it.
    /// </summary>
    public IEnumerable<FileSystemPath> AuthoredFolders
        => this.SourceMounts.Select(static mount => mount.Location!)
            .Where(folder => !folder.IsSameOrInside(this.ProjectRoot))
            .Prepend(this.ProjectRoot);

    /// <summary>Gets the cooked mounts that are bound and valid: the libraries the project reads.</summary>
    public IEnumerable<ResolvedMount> LibraryMounts
        => this.CookedMounts.Where(static mount => mount.IsBound);

    /// <summary>Gets every cooked mount, whatever its state, as the Content Explorer lists them.</summary>
    public IEnumerable<ResolvedMount> CookedMounts
        => this.Mounts.Where(static mount => mount.Kind == MountKind.Cooked);

    /// <inheritdoc/>
    /// <remarks>A library's file is found through its facts; what a pak holds has no file of its own.</remarks>
    public FileSystemPath? LocateReference(AssetPath referrer, string text)
        => AssetPath.TryResolveReference(referrer, text, VirtualPathRuleSet.Cooked, out var path, out _) ? this.Find(path)?.File : null;

    /// <summary>Gets the mounts the project commits, as <c>Project.oxy</c> declares them.</summary>
    public IEnumerable<ProjectMount> DeclaredMounts
        => this.Mounts.Select(static mount => mount.Declaration).OfType<ProjectMount>();

    /// <summary>Gets the output folders the Content Explorer shows; view state, never mounts.</summary>
    public IReadOnlyList<OutputFolderView> OutputFolderViews { get; init; } = [];

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
    ///     Gets the scope of the project's user-local settings in the editor database: the project root, compared without
    ///     case as Windows paths are. A change of the content mounts keeps the root, so it keeps the scope too.
    /// </summary>
    /// <remarks>
    ///     A path can be reused by another project, so a stored value carries the project id it was written for.
    /// </remarks>
    public SettingContext SettingsScope => SettingContext.Project(this.ProjectRoot.FullPath.ToUpperInvariant());

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

        var table = ProjectMounts.Resolve(fileSystem, FileSystemPath.From(info.Location), info.Mounts);
        return new ProjectContext
        {
            ProjectId = info.Id,
            Name = info.Name,
            Category = info.Category,
            ProjectRoot = FileSystemPath.From(info.Location),
            FileSystem = fileSystem,
            Thumbnail = info.Thumbnail,
            Mounts = table.Mounts,
            MountFindings = table.Findings,
            OutputFolderViews = [.. info.OutputFolderViews],
            CookedContentOrder = [.. info.CookedContentOrder],
            DefaultSceneId = info.DefaultSceneId,
            Settings = info.Settings,
        };
    }
}
