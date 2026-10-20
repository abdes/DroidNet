// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Text;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Testing;

/// <summary>
/// A project for a test: a <see cref="ProjectContext"/> whose root and authoring mount folders exist on a file system
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
    /// <param name="authoringMounts">The authoring mounts, each in the folder of its name; <see cref="ContentMount"/>
    /// when none is named.</param>
    /// <returns>The project.</returns>
    public static TestProject Create(IFileSystem fileSystem, string? root = null, IReadOnlyList<string>? authoringMounts = null)
    {
        ArgumentNullException.ThrowIfNull(fileSystem);
        IReadOnlyList<string> mounts = authoringMounts is { Count: > 0 } ? authoringMounts : [ContentMount];
        var context = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Test project",
            Category = Category.Games,
            ProjectRoot = FileSystemPath.From(root ?? DefaultRoot),
            FileSystem = fileSystem,
            AuthoringMounts = [.. mounts.Select(static mount => new ProjectMountPoint(mount, mount))],
            LocalFolderMounts = [],
        };
        foreach (var mount in mounts)
        {
            _ = fileSystem.Directory.CreateDirectory(context.ProjectRoot.Combine(mount).FullPath);
        }

        return new TestProject(context);
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
