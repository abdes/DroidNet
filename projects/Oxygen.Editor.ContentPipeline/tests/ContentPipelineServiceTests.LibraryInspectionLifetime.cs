// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Mounting;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks native inspection ownership during failed worker termination.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Neither metadata refresh nor mount preparation releases a library while its Inspector still owns it.</summary>
    /// <param name="mounting">Whether the inspection is part of mount preparation.</param>
    /// <returns>The asynchronous ownership regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task LibraryInspectionRetainsReadersAndWriterUntilDrain(bool mounting)
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
        var work = mounting ? consumer.CookCoordinator.RunAsync(
            async (_, token) =>
        {
            using var prepared = await new CookedContentMountService(new NativeStorageProvider(new RealFileSystem()), api).PrepareAsync(context, [], Disposable.Empty, token).ConfigureAwait(false);
            return true;
        },
            this.TestContext.CancellationToken)
            : service.RefreshLibraryMetadataAsync(context, this.TestContext.CancellationToken);
        await this.AssertReaderRetainedUntilDrainAsync(work, consumer, openForWrite, drain).ConfigureAwait(false);
    }
}
