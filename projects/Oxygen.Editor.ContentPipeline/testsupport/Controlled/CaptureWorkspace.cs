// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using static Oxygen.Editor.ContentPipeline.TestSupport.SnapshotScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class CaptureWorkspace : IDisposable
{
    private readonly ProjectContextService context = new();

    public CaptureWorkspace()
    {
        Directory.CreateDirectory(this.Root);
        this.context.Activate(new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Capture tests",
            Category = Category.Games,
            ProjectRoot = this.Root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        });
        this.Coordinator = new(this.context, NullLogger<ContentCookCoordinator>.Instance);
        this.Capture = new(this.Documents, this.Coordinator);
    }

    public string Root { get; } = Path.Combine(Path.GetTempPath(), "OxygenCookCaptureTests", Guid.NewGuid().ToString("N"));

    public CookDocumentRegistry Documents { get; } = new();

    public ContentCookCoordinator Coordinator { get; }

    public CookInputSnapshotCapture Capture { get; }

    public CookSnapshotInput WriteInput(string relativePath, string content)
    {
        var source = Path.GetFullPath(Path.Combine(this.Root, relativePath));
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, content);
        return new(new Uri("asset:///" + relativePath), source, relativePath, Hash(content));
    }

    public Task<CookSnapshotCaptureResult> CaptureAsync(IReadOnlyList<CookSnapshotInput> inputs, string build = "compatible-build")
        => this.CaptureAsync(_ => Task.FromResult(inputs), build);

    public Task<CookSnapshotCaptureResult> CaptureAsync(
        Func<CancellationToken, Task<IReadOnlyList<CookSnapshotInput>>> discover, string build = "compatible-build")
        => this.Coordinator.RunAsync((operation, token) => this.Capture.CaptureAsync(operation, discover, build, token), CancellationToken.None);

    public void Dispose()
    {
        this.context.Close();
        this.Coordinator.Dispose();
        Directory.Delete(this.Root, recursive: true);
    }
}
