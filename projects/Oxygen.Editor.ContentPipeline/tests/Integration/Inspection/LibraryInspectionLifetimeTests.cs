// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class LibraryInspectionLifetimeTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Inventory failure returns promptly while native readers retain files and publication ownership.</summary>
    /// <param name="duringCook">Whether inventory runs during cook planning or explicit validation.</param>
    /// <returns>The asynchronous inventory lifetime regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(true)]
    [DataRow(false)]
    public async Task InventoryTerminationTransfersCleanupWithoutBlockingFailure(bool duringCook)
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new ContextLeaseRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var source = new Uri("asset:///Content/Materials/Shared.omat.json");
        AssertCookSucceeded(await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false));
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Action openForWrite = () =>
        {
            using var file = new FileStream(Path.Combine(workspace.CookedRoot("Content"), "container.index.bin"), FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete);
        };
        runner.BeforeInventoryInspection = () => Task.FromException(new ContentPipelineTerminationException(new IOException("Simulated inventory termination failure."), drain.Task));
        try
        {
            if (duringCook)
            {
                await AssertReaderRetainedUntilDrainAsync(this.TestContext, service.CookAssetAsync(source, this.TestContext.CancellationToken), workspace, openForWrite, drain).ConfigureAwait(false);
            }
            else
            {
                Func<Task> work = () => service.InspectCookedOutputAsync(null, this.TestContext.CancellationToken, validate: true)
                    .WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken);
                var failure = await work.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
                _ = openForWrite.Should().Throw<IOException>();
                using (var writer = await CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false))
                {
                    _ = writer.ProjectRoot.Should().Be(workspace.Root);
                }

                drain.SetResult();
                await failure.Which.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
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
        using var library = new CookWorkspace();
        using var consumer = new CookWorkspace();
        library.WriteMaterial("Content/Materials/Shared.omat.json", "Shared");
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var runner = new ContextLeaseRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var producer = CreateService(library, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        _ = (await producer.CookAssetAsync(new("asset:///Content/Materials/Shared.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        var root = library.CookedRoot("Content");
        var context = consumer.ProjectContext with { LocalFolderMounts = [new("Library", root)] };
        consumer.Activate(context);
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Action openForWrite = () => { using var file = new FileStream(Path.Combine(root, "container.index.bin"), FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete); };
        runner.BeforeDependencyInspection = () =>
        {
            _ = openForWrite.Should().Throw<IOException>();
            return Task.FromException(new ContentPipelineTerminationException(new IOException("Simulated Inspector termination failure."), drain.Task));
        };
        var service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var work = service.RefreshLibraryMetadataAsync(context, this.TestContext.CancellationToken);
        await AssertReaderRetainedUntilDrainAsync(this.TestContext, work, consumer, openForWrite, drain).ConfigureAwait(false);
    }
}
