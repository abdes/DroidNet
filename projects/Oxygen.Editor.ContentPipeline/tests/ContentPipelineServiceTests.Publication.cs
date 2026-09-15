// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Runs the real native producer through staged publication and injected runtime failures.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A native recook followed by mount failure restores every published byte and both metadata files.</summary>
    /// <returns>The asynchronous native publication regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeCookWithFailedPreviewRestoresPublishedGeneration()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var publication = new CookPublicationService(workspace.CookCoordinator, workspace.ContextService, new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        var previous = ReadOutputIdentities(workspace.Root);
        var provenancePath = Path.Combine(workspace.Root, ".build", "cook", "provenance.json");
        var priorProvenance = await File.ReadAllBytesAsync(provenancePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.WriteText("Content/Materials/Blue.omat.json", workspace.ReadText("Content/Materials/Blue.omat.json").Replace("0.5", "0.8", StringComparison.Ordinal));
        var preview = new FailingPublicationPreview();
        await using var previewLifetime = preview.ConfigureAwait(false);
        using var registration = publication.RegisterPreview(workspace.ProjectContext, () => Task.FromResult<ICookPublicationPreview?>(preview));

        var failed = await pipeline.CookAssetAsync(new Uri("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = failed.Validation!.Succeeded.Should().BeTrue();
        _ = failed.Diagnostics.Should().Contain(item => item.Code == "Cook.PublicationFailed");
        var restored = ReadOutputIdentities(workspace.Root);
        _ = restored.ToDictionary(static pair => pair.Key, static pair => pair.Value.hash, StringComparer.Ordinal)
            .Should().BeEquivalentTo(previous.ToDictionary(static pair => pair.Key, static pair => pair.Value.hash, StringComparer.Ordinal));
        var receiptPath = Path.Combine(workspace.Root, ".cooked", "publication.json");
        _ = restored.Where(pair => !string.Equals(pair.Key, receiptPath, StringComparison.Ordinal)).Should()
            .BeEquivalentTo(previous.Where(pair => !string.Equals(pair.Key, receiptPath, StringComparison.Ordinal)));
        _ = (await File.ReadAllBytesAsync(provenancePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(priorProvenance);
        _ = preview.Mounts.Should().Be(2);
        _ = api.Imported.Should().OnlyContain(execution => execution.Manifest.Output.Contains(Path.Combine(".build", "cook"), StringComparison.Ordinal));
        registration.Dispose();
        AssertCookSucceeded(await pipeline.CookAssetAsync(new Uri("asset:///Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>Recovery skips live operation ownership and restores the same journal after its owner releases it.</summary>
    /// <returns>The asynchronous recovery-orchestration regression.</returns>
    [TestMethod]
    public async Task RecoveryDistinguishesLiveAndAbandonedPreparedOperations()
    {
        using var workspace = new TempWorkspace();
        var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
        var publication = new CookPublicationService(workspace.CookCoordinator, workspace.ContextService, files);
        var operation = new ContentCookOperation(Guid.NewGuid(), workspace.ProjectContext, 1);
        var index = Path.Combine(workspace.Root, ".cooked", "Content", "container.index.bin");
        await File.WriteAllTextAsync(index, "old", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var staging = await CookStagingArea.CreateAsync(operation, ["Content"], this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(staging.Roots[0].StagingPath, "container.index.bin"), "new", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await CookPublicationTransaction.PrepareAsync(
            operation,
            staging,
            new Dictionary<string, byte[]>(StringComparer.Ordinal)
            {
                [CookPublicationTransaction.PublicationMetadata] = System.Text.Encoding.UTF8.GetBytes("new-receipt"),
            },
            files,
            this.TestContext.CancellationToken).ConfigureAwait(false);
        await publication.RecoverBeforeCookAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Directory.Exists(staging.Roots[0].StagingPath).Should().BeTrue();
        staging.Dispose();
        await publication.RecoverBeforeCookAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Directory.Exists(staging.Roots[0].StagingPath).Should().BeFalse();
        _ = (await File.ReadAllTextAsync(index, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("old");
        using var writer = await CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var recovered = await CookPublicationTransaction.LoadAsync(workspace.ProjectContext, operation.OperationId, files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = recovered.Phase.Should().Be(CookPublicationPhase.RolledBack);
    }

    /// <summary>A verified startup generation remains protected until the runtime takes its reader lease.</summary>
    /// <returns>The asynchronous startup ownership regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task StartupVerificationKeepsReadOwnershipThroughMountHandoff()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var publication = new CookPublicationService(workspace.CookCoordinator, workspace.ContextService, new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        using var mounted = await publication.AcquireForMountAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        Action replace = () => CookOutputLease.AcquireWrite(workspace.Root).Dispose();
        _ = replace.Should().Throw<CookOutputBusyException>();
        mounted.Dispose();
        replace();
    }

    /// <summary>Corrupt generation metadata blocks mounting but an ordinary cook can rebuild it.</summary>
    /// <returns>The asynchronous metadata repair regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookRepairsCorruptPublicationMetadata()
    {
        using var workspace = new TempWorkspace();
        await PrepareIncrementalSceneAsync(workspace).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var publication = new CookPublicationService(workspace.CookCoordinator, workspace.ContextService, new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility, publication);
        AssertCookSucceeded(await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false));
        await File.WriteAllTextAsync(Path.Combine(workspace.Root, ".cooked", "publication.json"), "invalid receipt", this.TestContext.CancellationToken).ConfigureAwait(false);
        var repaired = await pipeline.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(repaired);
        _ = repaired.IsPublished.Should().BeTrue();
        using var mounted = await publication.AcquireForMountAsync(workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private sealed partial class FailingPublicationPreview : ICookPublicationPreview
    {
        public bool IsRuntimeAvailable => true;

        public int Mounts { get; private set; }

        public Task PrepareReplacementAsync() => Task.CompletedTask;

        public Task MountAsync(IReadOnlyList<string> roots, CookOutputWriteLease? writer)
        {
            this.Mounts++;
            return this.Mounts == 1 ? Task.FromException(new IOException("Injected preview failure")) : Task.CompletedTask;
        }

        public Task ResumeAsync() => Task.CompletedTask;

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
