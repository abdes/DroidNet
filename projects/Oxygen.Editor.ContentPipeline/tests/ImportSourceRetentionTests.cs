// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies atomic retention of complete source bundles before derived processing.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class ImportSourceRetentionTests
{
    /// <summary>Gets or sets the test cancellation context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Retained sources preserve relative dependency paths and survive a later cook failure or original-file removal.</summary>
    /// <returns>The asynchronous retention regression.</returns>
    [TestMethod]
    public async Task RetainedBundleSurvivesOriginalRemovalAndLaterFailure()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "{\"buffers\":[{\"uri\":\"buffers/data.bin\"}]}");
        var buffer = workspace.Write("buffers/data.bin", "vertices");
        RetainedImportSource? retained = null;
        var operation = () => workspace.Coordinator.RunAsync<int>(
            async (owner, token) =>
            {
                retained = (await workspace.Retention.RetainAsync(owner, "Model", primary.RelativePath, _ => Task.FromResult<IReadOnlyList<CookSnapshotInput>>([primary, buffer]), token).ConfigureAwait(false)).Source;
                throw new InvalidDataException("Subsequent native cook failed.");
            },
            this.TestContext.CancellationToken);
        _ = await operation.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        File.Delete(primary.SourcePath);
        File.Delete(buffer.SourcePath);
        _ = retained.Should().NotBeNull();
        _ = retained!.DirectoryRelativePath.Should().Be("SourceMedia/Model");
        _ = retained.PrimaryRelativePath.Should().Be("model.gltf");
        _ = retained.Files.Should().Contain(new RetainedImportSourceFile("buffers/data.bin", buffer.DiscoveryHash));
        var retainedBuffer = Path.Combine(workspace.ProjectRoot, retained.DirectoryRelativePath, "buffers/data.bin");
        _ = (await File.ReadAllTextAsync(retainedBuffer, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("vertices");
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, ".cooked")).Should().BeFalse();
    }

    /// <summary>Existing source bundles cannot be overwritten by another import with the same destination.</summary>
    /// <returns>The asynchronous collision regression.</returns>
    [TestMethod]
    public async Task DestinationCollisionPreservesExistingSource()
    {
        using var workspace = new RetentionWorkspace();
        var input = workspace.Write("model.glb", "original");
        _ = await workspace.RetainAsync("Model", [input], this.TestContext.CancellationToken).ConfigureAwait(false);
        var replacement = workspace.Write("model.glb", "replacement");
        var retry = () => workspace.RetainAsync("Model", [replacement], this.TestContext.CancellationToken);
        _ = await retry.Should().ThrowAsync<IOException>().WithMessage("*already exists*").ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(Path.Combine(workspace.ProjectRoot, "SourceMedia/Model/model.glb"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("original");
    }

    /// <summary>Incomplete or changing discovery cannot expose a partially retained source folder.</summary>
    /// <param name="missing">Whether a dependency disappears or changes its bytes.</param>
    /// <returns>The asynchronous failure regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task DependencyFailureLeavesNoPartialBundle(bool missing)
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var dependency = workspace.Write("buffers/data.bin", "old");
        if (missing)
        {
            File.Delete(dependency.SourcePath);
        }
        else
        {
            await File.WriteAllTextAsync(dependency.SourcePath, "changed after discovery", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var retain = () => workspace.RetainAsync("Model", [primary, dependency], this.TestContext.CancellationToken);
        _ = await retain.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "SourceMedia/Model")).Should().BeFalse();
        _ = File.Exists(primary.SourcePath).Should().BeTrue();
    }

    /// <summary>Project closure during discovery cancels retention instead of copying into another activation.</summary>
    /// <returns>The asynchronous lifetime regression.</returns>
    [TestMethod]
    public async Task ClosingProjectDuringDiscoveryPreventsRetention()
    {
        using var workspace = new RetentionWorkspace();
        var input = workspace.Write("model.glb", "source");
        var retain = () => workspace.Coordinator.RunAsync(
            (owner, token) => workspace.Retention.RetainAsync(
                owner,
                "Model",
                input.RelativePath,
                _ =>
                {
                    workspace.Projects.Close();
                    return Task.FromResult<IReadOnlyList<CookSnapshotInput>>([input]);
                },
                token),
            this.TestContext.CancellationToken);
        _ = await retain.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "SourceMedia/Model")).Should().BeFalse();
    }

    /// <summary>Unsafe bundle destinations are rejected before exposure.</summary>
    /// <param name="bundleName">The proposed destination name.</param>
    /// <returns>The asynchronous destination regression.</returns>
    [TestMethod]
    [DataRow("../Content")]
    [DataRow("Model/Child")]
    [DataRow("Model:stream")]
    [DataRow("Model.")]
    public async Task InvalidDestinationIsRejected(string bundleName)
    {
        using var workspace = new RetentionWorkspace();
        var input = workspace.Write("model.glb", "source");
        var retain = () => workspace.RetainAsync(bundleName, [input], this.TestContext.CancellationToken);
        _ = await retain.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "SourceMedia")).Should().BeFalse();
    }

    /// <summary>Dirty participating source documents retain their existing save workflow and do not copy transient state.</summary>
    /// <returns>The asynchronous document-ownership regression.</returns>
    [TestMethod]
    public async Task DirtySourceReturnsSaveRequirementWithoutRetention()
    {
        using var workspace = new RetentionWorkspace();
        var input = workspace.Write("model.gltf", "saved");
        var document = new CookDocumentState(Guid.NewGuid(), input.SourcePath, "Model", 2, 1, IsDirty: true, input.DiscoveryHash);
        using var registration = workspace.Documents.Register(input.SourcePath, _ => Task.FromResult<CookDocumentReadLease?>(new(document, () => { })));
        var result = await workspace.RetainAsync("Model", [input], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Source.Should().BeNull();
        _ = result.NeedsSave.Should().ContainSingle().Which.Should().Be(document);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "SourceMedia")).Should().BeFalse();
    }

    /// <summary>A missing primary source or an ambiguous destination cannot become a retained bundle.</summary>
    /// <param name="missingPrimary">Whether the selected primary is absent or dependency destinations conflict.</param>
    /// <returns>The asynchronous bundle-membership regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task InvalidSourceSetCannotBeRetained(bool missingPrimary)
    {
        using var workspace = new RetentionWorkspace();
        var input = workspace.Write("model.glb", "source");
        IReadOnlyList<CookSnapshotInput> files = missingPrimary ? [input] : [input, input with { RelativePath = "MODEL.glb" }];
        var retain = () => workspace.Coordinator.RunAsync(
            (owner, token) => workspace.Retention.RetainAsync(owner, "Model", missingPrimary ? "missing.glb" : input.RelativePath, _ => Task.FromResult(files), token),
            this.TestContext.CancellationToken);
        if (missingPrimary)
        {
            _ = await retain.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        }
        else
        {
            _ = await retain.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        }

        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "SourceMedia/Model")).Should().BeFalse();
    }

    private sealed partial class RetentionWorkspace : IDisposable
    {
        private readonly string root = Directory.CreateTempSubdirectory("OxygenSourceRetention-").FullName;

        public RetentionWorkspace()
        {
            Directory.CreateDirectory(this.ProjectRoot);
            this.Projects.Activate(new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Retention", Category = Category.Games, ProjectRoot = this.ProjectRoot,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
            });
            this.Coordinator = new(this.Projects, NullLogger<ContentCookCoordinator>.Instance);
            this.Retention = new(this.Documents, this.Coordinator);
        }

        public string ProjectRoot => Path.Combine(this.root, "Project");

        public ProjectContextService Projects { get; } = new();

        public CookDocumentRegistry Documents { get; } = new();

        public ContentCookCoordinator Coordinator { get; }

        public ImportSourceRetention Retention { get; }

        public CookSnapshotInput Write(string relative, string text)
        {
            var path = Path.Combine(this.root, "Original", relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, text);
            return new(AssetUri: null, path, relative, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))));
        }

        public Task<ImportSourceRetentionResult> RetainAsync(string name, IReadOnlyList<CookSnapshotInput> inputs, CancellationToken cancellationToken)
            => this.Coordinator.RunAsync((owner, token) => this.Retention.RetainAsync(owner, name, inputs[0].RelativePath, _ => Task.FromResult(inputs), token), cancellationToken);

        public void Dispose()
        {
            this.Projects.Close();
            this.Coordinator.Dispose();
            Directory.Delete(this.root, recursive: true);
        }
    }
}
