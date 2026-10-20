// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>A temporary project folder with one <c>Content</c> authoring mount, deleted on disposal.</summary>
internal sealed class AssetWorkspace : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="AssetWorkspace"/> class.</summary>
    public AssetWorkspace()
    {
        this.Root = FileSystemPath.From(Path.Combine(Path.GetTempPath(), "oxygen-assets-tests", Guid.NewGuid().ToString("N")));
        _ = Directory.CreateDirectory(this.Root.FullPath);
    }

    /// <summary>Gets the project root folder.</summary>
    public FileSystemPath Root { get; }

    /// <summary>Resolves a project-relative path.</summary>
    /// <param name="relative">A path relative to the project root, with forward slashes.</param>
    /// <returns>The absolute path.</returns>
    public string SourcePath(string relative)
        => Path.Combine(this.Root.FullPath, relative.Replace('/', Path.DirectorySeparatorChar));

    /// <summary>Saves a project file for this folder and activates it.</summary>
    /// <returns>A project context service whose active project is this workspace.</returns>
    public ProjectContextService ActivateProject()
    {
        var service = new ProjectContextService();
        service.Activate(
            TestProject.Create(new RealFileSystem(), this.Root.FullPath).Context with { Name = "Test" });

        var context = service.ActiveProject!;
        var info = new ProjectInfo(context.ProjectId, context.Name, context.Category, context.ProjectRoot.FullPath)
        {
            AuthoringMounts = [.. context.AuthoringMounts],
        };
        File.WriteAllText(Path.Combine(this.Root.FullPath, "Project.oxy"), ProjectInfo.ToJson(info));
        return service;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (Directory.Exists(this.Root.FullPath))
        {
            Directory.Delete(this.Root.FullPath, recursive: true);
        }
    }
}
