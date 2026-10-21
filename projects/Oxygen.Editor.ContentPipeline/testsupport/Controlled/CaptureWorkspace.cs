// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;
using static Oxygen.Editor.ContentPipeline.TestSupport.SnapshotScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class CaptureWorkspace : IDisposable
{
    private readonly TestSession session;

    public CaptureWorkspace()
    {
        Directory.CreateDirectory(this.Root);
        this.session = new(TestProject.Create(new RealFileSystem(), this.Root).Context with { Name = "Capture tests" });
        this.Coordinator = new(this.session, NullLogger<ContentCookCoordinator>.Instance);
        this.Capture = new(this.Coordinator);
    }

    public string Root { get; } = Path.Combine(Path.GetTempPath(), "OxygenCookCaptureTests", Guid.NewGuid().ToString("N"));

    public ContentCookCoordinator Coordinator { get; }

    public CookInputSnapshotCapture Capture { get; }

    public CookSnapshotInput WriteInput(string relativePath, string content)
    {
        var source = Path.GetFullPath(Path.Combine(this.Root, relativePath));
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, content);
        return new(AssetPath.Parse("/" + relativePath), FileSystemPath.From(source), relativePath, Hash(content));
    }

    public Task<CookInputSnapshot> CaptureAsync(IReadOnlyList<CookSnapshotInput> inputs, string build = "compatible-build")
        => this.CaptureAsync(_ => Task.FromResult(inputs), build);

    public Task<CookInputSnapshot> CaptureAsync(
        Func<CancellationToken, Task<IReadOnlyList<CookSnapshotInput>>> discover, string build = "compatible-build")
        => this.Coordinator.RunAsync((operation, token) => this.Capture.CaptureAsync(operation, discover, build, token), CancellationToken.None);

    public void Dispose()
    {
        this.session.End();
        this.Coordinator.Dispose();
        this.session.Dispose();
        Directory.Delete(this.Root, recursive: true);
    }
}
