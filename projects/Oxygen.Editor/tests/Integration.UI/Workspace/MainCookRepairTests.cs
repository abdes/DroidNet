// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.TestSupport.NativeSceneData;
using static Oxygen.Editor.TestSupport.PublicationWorkflows;

namespace Oxygen.Editor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class MainCookRepairTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A captured -1 remains in failure history while a saved repair to 100 becomes current and survives reopening.</summary>
    /// <returns>The complete Main repair journey.</returns>
    [TestMethod]
    public Task MainCookRepairPreservesFailureAndPublishesSavedOneHundred() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            scene.Name = "Main";
            AddGeometryNode(scene, "Cube");
            var environment = scene.Environment;
            scene.Hydrate(scene.Dehydrate() with { Environment = environment with { SkyAtmosphere = environment.SkyAtmosphere with { AerialPerspectiveStartDepthMeters = 0 } } });
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(90));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        _ = (await fixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
        using var services = new CatalogWorkloadServices(fixture);
        await using var catalog = new AssetCatalog(services.Projects, services.Storage, services.EngineContent, services.Publication);
        await using var publication = await fixture.StartWorkspacePreviewAsync(services, catalog).ConfigureAwait(true);
        var uri = AssetPath.Parse("/Content/Scenes/Main.oscene");
        _ = (await services.Pipeline.CookCurrentSceneAsync(uri, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        var originalOutput = await ReadPublishedAsync(services, timeout.Token).ConfigureAwait(true);
        await fixture.SetSavedLegacyAerialStartAsync().ConfigureAwait(true);
        var failed = await services.Pipeline.CookCurrentSceneAsync(uri, timeout.Token).ConfigureAwait(true);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = (await ReadPublishedAsync(services, timeout.Token).ConfigureAwait(true)).Should().BeEquivalentTo(originalOutput);
        await CheckFailedRunAsync(services, failed.OperationId, timeout.Token).ConfigureAwait(true);
        await RepairAerialStartAsync(fixture).ConfigureAwait(true);
        _ = (await fixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = (await services.Pipeline.CookCurrentSceneAsync(uri, timeout.Token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
        var retried = services.Runs.Runs.OrderByDescending(static run => run.StartedAt).First();
        _ = retried.OperationId.Should().NotBe(failed.OperationId);
        _ = retried.State.Should().BeOneOf(CookRunState.Succeeded, CookRunState.SucceededWithWarnings);
        _ = (await services.Pipeline.ReadAsync(services.Projects.ActiveProject!, [uri], timeout.Token).ConfigureAwait(true)).Single().Freshness.Should().Be(AssetCookFreshness.Current);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = fixture.Source.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = (await fixture.ReadNativeAsync(timeout.Token).ConfigureAwait(true)).AerialPerspectiveStartDepthMeters.Should().Be(100);
        _ = services.Runs.Runs.Single(run => run.OperationId == failed.OperationId).Messages.Should().Contain(message => message.Text.Contains("= -1", StringComparison.Ordinal));
    });

    // The failed run keeps the captured value and names the property that repairs it.
    private static async Task CheckFailedRunAsync(CatalogWorkloadServices services, Guid failedId, CancellationToken cancellationToken)
    {
        CookRunSnapshot? run;
        while ((run = services.Runs.Runs.SingleOrDefault(value => value.OperationId == failedId)) is not { State: CookRunState.Failed })
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        _ = run.Messages.Should().Contain(message => message.Text.Contains("= -1", StringComparison.Ordinal));
        _ = run.Diagnostics.Should().Contain(value => value.Message.Contains("0 m", StringComparison.Ordinal)
            && value.SuggestedAction != null
            && string.Equals(value.SuggestedAction.Payload["PropertyPath"], SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal));
    }

    // Authoring refuses a value the cook would refuse, and accepts the repair.
    private static async Task RepairAerialStartAsync(NativeSceneFixture fixture)
    {
        var aerialStart = SceneProperties.SceneEnvironment.AerialPerspectiveStartDepthMeters;
        _ = (await fixture.Authoring.ExecuteAsync(new SetSceneProperties(PropertyEdit.SingleEdit(aerialStart, -2f), "Aerial Start")).ConfigureAwait(true)).Succeeded.Should().BeFalse();
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = fixture.Source.Environment.SkyAtmosphere.AerialPerspectiveStartDepthMeters.Should().Be(-1);
        _ = (await fixture.Authoring.ExecuteAsync(new SetSceneProperties(PropertyEdit.SingleEdit(aerialStart, 100f), "Aerial Start")).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = fixture.Document.IsDirty.Should().BeTrue();
    }
}
