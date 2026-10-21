// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>A temporary project folder with one <c>Content</c> authoring mount, deleted on disposal.</summary>
internal sealed partial class AssetWorkspace : IDisposable
{
    private readonly List<TestSession> sessions = [];

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

    /// <summary>Saves a project file for this folder and opens a session of it, which the workspace disposes.</summary>
    /// <returns>The session of the project in this workspace.</returns>
    public TestSession OpenSession()
    {
        var session = new TestSession(TestProject.Create(new RealFileSystem(), this.Root.FullPath).Context with { Name = "Test" });
        this.sessions.Add(session);
        _ = TestProject.SaveProjectFile(session.Context);
        return session;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var session in this.sessions)
        {
            session.Dispose();
        }

        if (Directory.Exists(this.Root.FullPath))
        {
            Directory.Delete(this.Root.FullPath, recursive: true);
        }
    }
}
