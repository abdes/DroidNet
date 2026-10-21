// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed partial class RetentionWorkspace : IDisposable
{
    private readonly string root = Directory.CreateTempSubdirectory("OxygenSourceRetention-").FullName;

    public RetentionWorkspace()
    {
        Directory.CreateDirectory(this.ProjectRoot);
        this.Projects.Activate(TestProject.Create(new RealFileSystem(), this.ProjectRoot).Context with { Name = "Retention" });
        this.Coordinator = new(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
        this.Retention = new(this.Coordinator);
    }

    public string ProjectRoot => Path.Combine(this.root, "Project");

    public ProjectContextService Projects { get; } = new();

    public ContentCookCoordinator Coordinator { get; }

    public ImportSourceRetention Retention { get; }

    public CookSnapshotInput Write(string relative, string text)
    {
        var path = Path.Combine(this.root, "Original", relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
        return new(AssetUri: null, FileSystemPath.From(path), relative, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))));
    }

    public Task<RetainedImportSource> RetainAsync(string name, IReadOnlyList<CookSnapshotInput> inputs, CancellationToken cancellationToken)
        => this.Coordinator.RunAsync((owner, token) => this.Retention.RetainAsync(owner, AssetPath.Parse("/Content/Models"), name, _ => Task.FromResult(new ImportSourceBundle(inputs[0].RelativePath, inputs.ToImmutableArray())), token), cancellationToken);

    public void Dispose()
    {
        this.Projects.Close();
        this.Coordinator.Dispose();
        Directory.Delete(this.root, recursive: true);
    }
}
