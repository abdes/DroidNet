// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks native inspection ownership during failed worker termination.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Inventory failure returns promptly while native readers retain files and publication ownership.</summary>
    /// <param name="duringCook">Whether inventory runs during cook planning or explicit validation.</param>
    /// <returns>The asynchronous inventory lifetime regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(true)]
    [DataRow(false)]
    public async Task InventoryTerminationTransfersCleanupWithoutBlockingFailure(bool duringCook)
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new ContextLeaseRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var source = new Uri("asset:///Content/Materials/Shared.omat.json");
        AssertCookSucceeded(await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false));
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Action openForWrite = () =>
        {
            using var file = new FileStream(Path.Combine(workspace.Root, ".cooked/Content/container.index.bin"), FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete);
        };
        runner.BeforeInventoryInspection = () => Task.FromException(new ContentPipelineTerminationException(new IOException("Simulated inventory termination failure."), drain.Task));
        try
        {
            if (duringCook)
            {
                await this.AssertReaderRetainedUntilDrainAsync(service.CookAssetAsync(source, this.TestContext.CancellationToken), workspace, openForWrite, drain).ConfigureAwait(false);
            }
            else
            {
                Func<Task> work = () => service.InspectCookedOutputAsync(null, this.TestContext.CancellationToken, validate: true)
                    .WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken);
                var failure = await work.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
                _ = openForWrite.Should().Throw<IOException>();
                Action publish = () => { using var writer = CookOutputLease.AcquireWrite(workspace.Root); };
                _ = publish.Should().Throw<CookOutputBusyException>();
                drain.SetResult();
                await failure.Which.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
                publish();
            }

            openForWrite();
        }
        finally
        {
            _ = drain.TrySetResult();
        }
    }

    /// <summary>Explicit metadata inspection retains a library while its Inspector still owns it.</summary>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task LibraryInspectionRetainsReadersAndWriterUntilDrain()
    {
        using var library = new TempWorkspace();
        using var consumer = new TempWorkspace();
        library.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new ContextLeaseRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await producer.CookAssetAsync(new("asset:///Content/Materials/Shared.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var root = Path.Combine(library.Root, ".cooked/Content");
        var context = consumer.ProjectContext with { LocalFolderMounts = [new("Library", root)] };
        consumer.ContextService.Activate(context);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Action openForWrite = () => { using var file = new FileStream(Path.Combine(root, "container.index.bin"), FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete); };
        runner.BeforeDependencyInspection = () =>
        {
            _ = openForWrite.Should().Throw<IOException>();
            return Task.FromException(new ContentPipelineTerminationException(new IOException("Simulated Inspector termination failure."), drain.Task));
        };
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var work = service.RefreshLibraryMetadataAsync(context, this.TestContext.CancellationToken);
        await this.AssertReaderRetainedUntilDrainAsync(work, consumer, openForWrite, drain).ConfigureAwait(false);
    }
}
