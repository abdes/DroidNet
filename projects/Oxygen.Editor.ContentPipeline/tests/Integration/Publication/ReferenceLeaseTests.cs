// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ReferenceLeaseTests
{
    public TestContext TestContext { get; set; } = null!;

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
        using var workspace = new CookWorkspace([new("Content", "Content"), new("Art", "Art")]);
        using var foreignConsumer = new CookWorkspace();
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
            consumer.Activate(consumer.ProjectContext with { LocalFolderMounts = [new("Library", workspace.CookedRoot("Art"))] });
            service = CreateService(consumer, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        }

        AddGeometryNode(consumer, geometry, "Mesh");
        await consumer.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var index = Path.Combine(workspace.CookedRoot("Art"), "container.index.bin");
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
                await AssertReaderRetainedUntilDrainAsync(this.TestContext, work, consumer, openForWrite, drain).ConfigureAwait(false);
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
}
