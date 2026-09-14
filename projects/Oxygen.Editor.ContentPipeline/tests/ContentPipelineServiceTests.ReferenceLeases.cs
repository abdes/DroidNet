// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises unchanged context-root ownership through native execution and failed termination.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Reused dependency files stay protected until the worker and its cleanup have drained.</summary>
    /// <param name="terminationFailure">Whether native work reports a delayed termination failure.</param>
    /// <param name="foreignLibrary">Whether the dependency belongs to a different project.</param>
    /// <returns>The asynchronous reference-reader ownership regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(true, true)]
    public async Task ReusedCrossMountRootsStayLeasedThroughNativeWork(bool terminationFailure, bool foreignLibrary)
    {
        using var workspace = new TempWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var foreignConsumer = new TempWorkspace();
        var source = await WriteCrossMountModelAsync(workspace, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new ContextLeaseRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var first = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.IsPublished.Should().BeTrue();
        var geometry = first.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry).CookedAssetUri;
        var consumer = foreignLibrary ? foreignConsumer : workspace;
        if (foreignLibrary)
        {
            consumer.ContextService.Activate(consumer.ProjectContext with { LocalFolderMounts = [new("Library", Path.Combine(workspace.Root, ".cooked/Art"))] });
            service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        }

        AddGeometryNode(consumer, geometry, "Mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var index = Path.Combine(workspace.Root, ".cooked/Art/container.index.bin");
        Action openForWrite = () => { using var file = new FileStream(index, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite | FileShare.Delete); };
        var observed = false;
        runner.BeforeBatch = () =>
        {
            _ = openForWrite.Should().Throw<IOException>();
            observed = true;
            return terminationFailure ? Task.FromException(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task)) : Task.CompletedTask;
        };
        try
        {
            var work = service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken);
            if (terminationFailure)
            {
                await this.AssertReaderRetainedUntilDrainAsync(work, consumer, openForWrite, drain).ConfigureAwait(false);
            }
            else
            {
                _ = (await work.ConfigureAwait(false)).IsPublished.Should().BeTrue();
            }

            _ = observed.Should().BeTrue();
            openForWrite();
        }
        finally
        {
            _ = drain.TrySetResult();
        }
    }

    private async Task AssertReaderRetainedUntilDrainAsync(Task work, TempWorkspace consumer, Action openForWrite, TaskCompletionSource drain)
    {
        Func<Task> observe = () => work;
        var failure = await observe.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = openForWrite.Should().Throw<IOException>();
        var next = consumer.CookCoordinator.RunAsync(
            (_, _) =>
        {
            openForWrite();
            return Task.FromResult(true);
        },
            this.TestContext.CancellationToken);
        _ = next.IsCompleted.Should().BeFalse();
        drain.SetResult();
        await failure.Which.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await next.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private sealed class ContextLeaseRunner : IContentPipelineProcessRunner
    {
        private readonly ContentPipelineProcessRunner inner = new();

        public Func<Task>? BeforeBatch { get; set; }

        public Func<Task>? BeforeDependencyInspection { get; set; }

        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            if (this.BeforeDependencyInspection is not null && request.Arguments.Contains("dependencies", StringComparer.Ordinal))
            {
                await this.BeforeDependencyInspection().ConfigureAwait(false);
            }

            if (this.BeforeBatch is not null && request.Arguments.Contains("batch", StringComparer.Ordinal))
            {
                await this.BeforeBatch().ConfigureAwait(false);
            }

            return await this.inner.RunAsync(request, cancellationToken).ConfigureAwait(false);
        }
    }
}
