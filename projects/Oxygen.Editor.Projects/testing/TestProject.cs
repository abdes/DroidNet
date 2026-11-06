// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text;
using System.Text.Json.Nodes;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Testing;

/// <summary>
/// A project for a test: a <see cref="ProjectContext"/> whose root and source mount folders exist on a file system
/// the test chooses, usually an in-memory one, and the files the test puts in it by virtual path.
/// </summary>
/// <remarks>
/// It is the one way the tests of other modules make a project, so a change to <see cref="ProjectContext"/> is made
/// here once. Files are resolved with the project's own mapping (<see cref="ProjectAssetFiles"/>), never by joining
/// strings.
/// </remarks>
public sealed class TestProject
{
    /// <summary>The name of the authoring mount a project has when the test names none.</summary>
    public const string ContentMount = "Content";

    /// <summary>The project folder a project has when the test names none.</summary>
    public const string DefaultRoot = @"C:\Project";

    private TestProject(ProjectContext context)
    {
        this.Context = context;
    }

    /// <summary>Gets the project, whose <see cref="ProjectContext.FileSystem"/> holds its files.</summary>
    public ProjectContext Context { get; }

    /// <summary>Gets the project's root folder.</summary>
    public FileSystemPath Root => this.Context.ProjectRoot;

    /// <summary>Creates a project and its folders.</summary>
    /// <param name="fileSystem">The file system that holds the project.</param>
    /// <param name="root">The project folder: <see cref="DefaultRoot"/> when none is given, which suits an in-memory file
    /// system; a temporary folder for the disk.</param>
    /// <param name="sourceMounts">The source mounts, each in the folder of its name; <see cref="ContentMount"/>
    /// when none is named.</param>
    /// <returns>The project.</returns>
    public static TestProject Create(IFileSystem fileSystem, string? root = null, IReadOnlyList<string>? sourceMounts = null)
    {
        var mounts = sourceMounts is { Count: > 0 } ? sourceMounts : [ContentMount];
        return Create(fileSystem, root, [.. mounts.Select(static mount => new ProjectMount(mount, MountKind.Source, mount))]);
    }

    /// <summary>
    /// Creates a project that declares the mounts the test names, and the folders of its source mounts that have a
    /// path. The mount table is resolved as the editor resolves it, <c>.mounts.json</c> files included.
    /// </summary>
    /// <param name="fileSystem">The file system that holds the project.</param>
    /// <param name="root">The project folder: <see cref="DefaultRoot"/> when none is given.</param>
    /// <param name="mounts">The declared mounts, each with its folder relative to the project folder or none.</param>
    /// <returns>The project.</returns>
    public static TestProject Create(IFileSystem fileSystem, string? root, IReadOnlyList<ProjectMount> mounts)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(mounts);
        var projectRoot = FileSystemPath.From(root ?? DefaultRoot);
        _ = fileSystem.Directory.CreateDirectory(projectRoot.FullPath);
        foreach (var mount in mounts.Where(static mount => mount is { Kind: MountKind.Source, Path: not null }))
        {
            _ = fileSystem.Directory.CreateDirectory(projectRoot.Combine(mount.Path!).FullPath);
        }

        var info = new ProjectInfo(Guid.NewGuid(), "Test project", Category.Games, projectRoot.FullPath) { Mounts = [.. mounts] };
        return Open(info, fileSystem);
    }

    /// <summary>Opens a project the test saved or a production service made, as the editor opens it.</summary>
    /// <param name="info">The saved project.</param>
    /// <param name="fileSystem">The file system that holds the project.</param>
    /// <returns>The project.</returns>
    public static TestProject Open(IProjectInfo info, IFileSystem fileSystem)
        => new(ProjectContext.FromProjectInfo(info, fileSystem));

    /// <summary>Gets the saved project a context is opened from.</summary>
    /// <param name="context">The project.</param>
    /// <returns>The project's saved form, which <see cref="Open"/> opens as <paramref name="context"/>.</returns>
    public static ProjectInfo InfoOf(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        return new ProjectInfo(context.ProjectId, context.Name, context.Category, context.ProjectRoot.FullPath, context.Thumbnail)
        {
            Mounts = [.. context.DeclaredMounts],
            OutputFolderViews = [.. context.OutputFolderViews],
            CookedContentOrder = [.. context.CookedContentOrder],
            DefaultSceneId = context.DefaultSceneId,
            Settings = context.Settings,
        };
    }

    /// <summary>Saves a context as its project file, through its file system, as code that loads the project reads it.</summary>
    /// <param name="context">The project.</param>
    /// <returns>The project file.</returns>
    public static FileSystemPath SaveProjectFile(ProjectContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        var file = context.ProjectRoot.Combine(Constants.ProjectFileName);
        context.FileSystem.File.WriteAllText(file.FullPath, ProjectInfo.ToJson(InfoOf(context)));
        return file;
    }

    /// <summary>Gets the text of a project file for a project the test describes, as <see cref="ProjectInfo.ToJson"/> writes it.</summary>
    /// <param name="mounts">The declared mounts: a <see cref="ContentMount"/> source mount when none are given.</param>
    /// <param name="schemaVersion">The schema version the file declares.</param>
    /// <param name="outputFolderView">The one output folder the project shows, or none.</param>
    /// <returns>The text of a <c>Project.oxy</c>.</returns>
    public static string ManifestJson(
        IReadOnlyList<ProjectMount>? mounts = null,
        int schemaVersion = ProjectInfo.CurrentSchemaVersion,
        string? outputFolderView = null)
    {
        var info = new ProjectInfo(Guid.NewGuid(), "Project", Category.Games, thumbnail: "Media/Preview.png", schemaVersion: schemaVersion)
        {
            Mounts = [.. mounts ?? new[] { new ProjectMount(ContentMount, MountKind.Source, ContentMount) }],
        };
        if (outputFolderView is not null)
        {
            info.OutputFolderViews = [new OutputFolderView(outputFolderView)];
        }

        return ProjectInfo.ToJson(info);
    }

    /// <summary>
    /// Binds mounts to folders or pak files on this machine, by writing the project's <c>.mounts.json</c> at its root
    /// through <paramref name="fileSystem"/>; it is the one test writer of that file.
    /// </summary>
    /// <param name="fileSystem">The file system that holds the project.</param>
    /// <param name="projectRoot">The project folder.</param>
    /// <param name="bindings">Each mount's name and its absolute folder or pak file.</param>
    /// <returns>The mounts file.</returns>
    public static FileSystemPath BindMounts(IFileSystem fileSystem, FileSystemPath projectRoot, params (string Name, string Location)[] bindings)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        ArgumentNullException.ThrowIfNull(projectRoot);
        var mounts = new JsonObject();
        foreach (var (name, location) in bindings)
        {
            mounts[name] = location.Replace('\\', '/');
        }

        var file = projectRoot.Combine(ProjectMounts.FileName);
        fileSystem.File.WriteAllText(file.FullPath, new JsonObject { ["schema_version"] = ProjectMounts.SchemaVersion, ["mounts"] = mounts }.ToJsonString());
        return file;
    }

    /// <summary>Gets the file or folder at a virtual path.</summary>
    /// <param name="path">A virtual path under one of the project's mounts, such as <c>/Content/Textures/Wood.png</c>.</param>
    /// <returns>The file or folder.</returns>
    public FileSystemPath PathOf(string path)
        => this.Context.Find(AssetPath.Parse(path))?.File
            ?? throw new ArgumentException($"'{path}' is not under one of the project's mounts.", nameof(path));

    /// <summary>Writes a file at a virtual path, creating its folders.</summary>
    /// <param name="path">A virtual path under one of the project's mounts.</param>
    /// <param name="content">The file's bytes.</param>
    /// <returns>The file.</returns>
    public FileSystemPath WriteFile(string path, byte[] content)
    {
        ArgumentNullException.ThrowIfNull(content);
        var file = this.PathOf(path);
        _ = this.Context.FileSystem.Directory.CreateDirectory(file.Parent!.FullPath);
        this.Context.FileSystem.File.WriteAllBytes(file.FullPath, content);
        return file;
    }

    /// <summary>Writes a text file at a virtual path, creating its folders.</summary>
    /// <param name="path">A virtual path under one of the project's mounts.</param>
    /// <param name="content">The file's text, written as UTF-8.</param>
    /// <returns>The file.</returns>
    public FileSystemPath WriteFile(string path, string content)
        => this.WriteFile(path, Encoding.UTF8.GetBytes(content ?? throw new ArgumentNullException(nameof(content))));
}
