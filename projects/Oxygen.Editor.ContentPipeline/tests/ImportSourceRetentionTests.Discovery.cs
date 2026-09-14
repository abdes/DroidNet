// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises native discovery and coherent source retention through the same project writer.</summary>
public sealed partial class ImportSourceRetentionTests
{
    /// <summary>Native discovery preserves embedded FBX and parent-relative glTF dependencies after original removal.</summary>
    /// <param name="extension">The retained source format.</param>
    /// <returns>The asynchronous native retention test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public async Task NativeDiscoveryRetainsPortableSourceBundle(string extension)
    {
        using var workspace = new RetentionWorkspace();
        var original = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), this.TestContext.CancellationToken).ConfigureAwait(false);
        byte[]? bufferBytes = null;
        if (string.Equals(extension, "gltf", StringComparison.Ordinal))
        {
            var document = JsonNode.Parse(original)!;
            var buffer = document["buffers"]![0]!;
            var embedded = buffer["uri"]!.GetValue<string>();
            bufferBytes = Convert.FromBase64String(embedded[(embedded.IndexOf(',', StringComparison.Ordinal) + 1)..]);
            buffer["uri"] = "../Shared/mesh%20data.bin";
            original = document.ToJsonString();
        }

        var primary = workspace.Write("Models/model." + extension, original);
        var originalRoot = Path.GetDirectoryName(Path.GetDirectoryName(primary.SourcePath))!;
        if (bufferBytes is not null)
        {
            var dependency = workspace.Write("Shared/mesh data.bin", string.Empty);
            await File.WriteAllBytesAsync(dependency.SourcePath, bufferBytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var native = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var retained = await RetainDiscoveredAsync(workspace, primary.SourcePath, native, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = retained.Source.Should().NotBeNull();
        var source = retained.Source!;
        Directory.Delete(originalRoot, recursive: true);
        var bundle = Path.Combine(workspace.ProjectRoot, source.DirectoryRelativePath);
        var expectedPrimary = bufferBytes is not null ? "Models/model.gltf" : "model.fbx";
        _ = source.PrimaryRelativePath.Should().Be(expectedPrimary);
        _ = (await File.ReadAllTextAsync(Path.Combine(bundle, expectedPrimary), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(original);
        _ = source.Files.Should().HaveCount(bufferBytes is null ? 1 : 2);
        if (bufferBytes is not null)
        {
            _ = (await File.ReadAllBytesAsync(Path.Combine(bundle, "Shared/mesh data.bin"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(bufferBytes);
        }

        foreach (var file in source.Files)
        {
            _ = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(Path.Combine(bundle, file.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false))).Should().Be(file.Sha256);
        }

        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, ".cooked")).Should().BeFalse();
    }

    /// <summary>A changed primary is rediscovered, including a changed common root and primary-relative path.</summary>
    /// <returns>The asynchronous coherent retry test.</returns>
    [TestMethod]
    public async Task ChangedPrimaryRetriesDiscoveryWithItsNewBundleLayout()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("Models/model.gltf", "old source");
        _ = workspace.Write("Models/data.bin", "old buffer");
        _ = workspace.Write("Shared/data.bin", "new buffer");
        var inspector = new DelegateSourceInspector(async (copy, token) =>
        {
            var captured = await File.ReadAllTextAsync(copy, token).ConfigureAwait(false);
            _ = copy.Should().NotBe(primary.SourcePath);
            if (string.Equals(captured, "old source", StringComparison.Ordinal))
            {
                await File.WriteAllTextAsync(primary.SourcePath, "new source", token).ConfigureAwait(false);
                _ = (await File.ReadAllTextAsync(copy, token).ConfigureAwait(false)).Should().Be("old source");
                return SourceFacts("data.bin");
            }

            return SourceFacts("../Shared/data.bin");
        });

        var result = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inspector.Copies.Should().HaveCount(2);
        _ = result.Source!.PrimaryRelativePath.Should().Be("Models/model.gltf");
        _ = result.Source.Files.Select(static file => file.RelativePath).Should().BeEquivalentTo("Models/model.gltf", "Shared/data.bin");
        _ = (await File.ReadAllTextAsync(Path.Combine(workspace.ProjectRoot, "Content/SourceMedia/DCC/Model/Models/model.gltf"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("new source");
        _ = inspector.Copies.Should().OnlyContain(path => !File.Exists(path));
    }

    /// <summary>Missing dependencies are reported by original path and never expose a partial bundle.</summary>
    /// <returns>The asynchronous missing-file test.</returns>
    [TestMethod]
    public async Task MissingDependencyReportsOriginalPathWithoutPartialRetention()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceInspector((_, _) => Task.FromResult(SourceFacts("missing.bin")));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false);
        _ = failure.Which.Diagnostics.Should().ContainSingle(issue => issue.Code == AssetImportDiagnosticCodes.SourceMissing
            && issue.AffectedPath == Path.Combine(Path.GetDirectoryName(primary.SourcePath)!, "missing.bin"));
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/SourceMedia/DCC/Model")).Should().BeFalse();
        _ = inspector.Copies.Should().OnlyContain(path => !File.Exists(path));
    }

    /// <summary>Unsupported source diagnostics identify the original file, not its temporary inspection copy.</summary>
    /// <returns>The asynchronous diagnostic mapping test.</returns>
    [TestMethod]
    public async Task UnsupportedSourceReportsOriginalLocation()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceInspector((copy, _) => Task.FromResult(SourceFacts() with
        {
            Supported = false,
            Diagnostics = [new() { OperationId = Guid.Empty, Domain = FailureDomain.AssetImport, Severity = DiagnosticSeverity.Error, Code = "import.unsupported", Message = "Unsupported source feature.", AffectedPath = copy }],
        }));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<CookInputDiscoveryException>().ConfigureAwait(false);
        _ = failure.Which.Diagnostics.Should().ContainSingle(issue => issue.AffectedPath == primary.SourcePath && issue.OperationId != Guid.Empty);
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/SourceMedia/DCC/Model")).Should().BeFalse();
    }

    /// <summary>Dirty sources block before any native query and preserve the established Save requirement.</summary>
    /// <returns>The asynchronous dirty-document test.</returns>
    [TestMethod]
    public async Task DirtySourceBlocksBeforeNativeDiscovery()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "saved");
        var state = new CookDocumentState(Guid.NewGuid(), primary.SourcePath, "Model", 2, 1, IsDirty: true, primary.DiscoveryHash);
        using var registration = workspace.Documents.Register(primary.SourcePath, _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => { })));
        var inspector = new DelegateSourceInspector((_, _) => Task.FromResult(SourceFacts()));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await retain.Should().ThrowAsync<CookInputsNeedSaveException>().ConfigureAwait(false);
        _ = inspector.Copies.Should().BeEmpty();
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/SourceMedia/DCC/Model")).Should().BeFalse();
    }

    /// <summary>Cancellation or project closure during native discovery cannot retain a bundle.</summary>
    /// <param name="closeProject">Whether the project closes or only the selected request is cancelled.</param>
    /// <returns>The asynchronous interrupted-query test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task InterruptedDiscoveryCleansItsCopyWithoutRetention(bool closeProject)
    {
        using var workspace = new RetentionWorkspace();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        var primary = workspace.Write("model.gltf", "source");
        var inspector = new DelegateSourceInspector(async (_, token) =>
        {
            if (closeProject)
            {
                workspace.Projects.Close();
            }
            else
            {
                await cancellation.CancelAsync().ConfigureAwait(false);
            }

            token.ThrowIfCancellationRequested();
            return SourceFacts();
        });
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, cancellation.Token).ConfigureAwait(false);
        _ = await retain.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = inspector.Copies.Should().ContainSingle();
        _ = inspector.Copies.Should().OnlyContain(path => !File.Exists(path));
        _ = Directory.Exists(Path.Combine(workspace.ProjectRoot, "Content/SourceMedia/DCC/Model")).Should().BeFalse();
    }

    /// <summary>Unacknowledged external source edits require reload before native inspection.</summary>
    /// <returns>The asynchronous saved-source ownership test.</returns>
    [TestMethod]
    public async Task ExternalSourceEditBlocksBeforeNativeDiscovery()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "saved");
        var state = new CookDocumentState(Guid.NewGuid(), primary.SourcePath, "Model", 1, 1, IsDirty: false, primary.DiscoveryHash);
        using var registration = workspace.Documents.Register(primary.SourcePath, _ => Task.FromResult<CookDocumentReadLease?>(new(state, () => { })));
        await File.WriteAllTextAsync(primary.SourcePath, "external edit", this.TestContext.CancellationToken).ConfigureAwait(false);
        var inspector = new DelegateSourceInspector((_, _) => Task.FromResult(SourceFacts()));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await retain.Should().ThrowAsync<IOException>().WithMessage("*changed outside the editor*").ConfigureAwait(false);
        _ = inspector.Copies.Should().BeEmpty();
    }

    /// <summary>Failed native termination retains the primary copy and operation marker until cleanup drains.</summary>
    /// <returns>The asynchronous retained-ownership test.</returns>
    [TestMethod]
    public async Task FailedTerminationRetainsPrimaryCopyAndOperationOwnership()
    {
        using var workspace = new RetentionWorkspace();
        var primary = workspace.Write("model.gltf", "source");
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var inspector = new DelegateSourceInspector((_, _) => Task.FromException<SceneSourceInspectionReport>(new ContentPipelineTerminationException(new IOException("Worker termination failed"), drain.Task)));
        Func<Task> retain = async () => _ = await RetainDiscoveredAsync(workspace, primary.SourcePath, inspector, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failure = await retain.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        var copy = inspector.Copies.Single();
        var marker = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(copy))!, "operation.lock");
        _ = File.Exists(copy).Should().BeTrue();
        Action acquire = () => { using var stream = File.Open(marker, FileMode.Open, FileAccess.ReadWrite, FileShare.None); };
        try
        {
            _ = acquire.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
        }

        await failure.Which.DrainCompletion.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = File.Exists(copy).Should().BeFalse();
        _ = File.Exists(marker).Should().BeFalse();
        _ = await workspace.Coordinator.RunAsync((_, _) => Task.FromResult(true), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private static SceneSourceInspectionReport SourceFacts(params string[] dependencies)
        => new(Parsed: true, Supported: true, "gltf", dependencies.ToImmutableArray(), new(1, "+X", "+Y", "+Z", IsLeftHanded: false, ReversesWinding: false), 1, 1, 1, []);

    private static Task<ImportSourceRetentionResult> RetainDiscoveredAsync(RetentionWorkspace workspace, string sourcePath, ISceneSourceInspector inspector, CancellationToken cancellationToken)
    {
        var discovery = new SceneImportSourceDiscovery(workspace.Documents, workspace.Coordinator, inspector);
        return workspace.Coordinator.RunAsync(
            (operation, token) => workspace.Retention.RetainAsync(
                operation,
                "Model",
                async captureToken => (await discovery.DiscoverAsync(operation, sourcePath, captureToken).ConfigureAwait(false)).Bundle,
                token),
            cancellationToken);
    }

    private sealed class DelegateSourceInspector(Func<string, CancellationToken, Task<SceneSourceInspectionReport>> inspect) : ISceneSourceInspector
    {
        public List<string> Copies { get; } = [];

        public Task<SceneSourceInspectionReport> InspectSceneSourceAsync(Guid operationId, string operationRoot, string sourcePath, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
        {
            this.Copies.Add(sourcePath);
            return inspect(sourcePath, cancellationToken);
        }
    }
}
