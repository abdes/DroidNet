// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks that queued publication respects later authoring intent and avoids unnecessary native refresh.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Publication cannot restore a binding that was undone, cleared or removed while queued.</summary>
    /// <param name="action">The later authored change.</param>
    /// <returns>The native lifetime regression.</returns>
    [TestMethod]
    [DataRow("Undo")]
    [DataRow("Clear")]
    [DataRow("RemoveGeometry")]
    public Task QueuedWorkspacePublicationKeepsLatestAssignment(string action)
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, (scenario, token) => CheckQueuedPublicationMutationAsync(scenario, action, token)));

    /// <summary>Repeating unchanged work preserves published files and leaves the native preview untouched.</summary>
    /// <param name="scope">The user-requested scope.</param>
    /// <returns>The native incremental-reuse regression.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Project)]
    public Task UnchangedWorkspaceCookDoesNotRefreshNativeBindings(CookTargetKind scope)
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope, (scenario, token) => CheckUnchangedWorkspaceCookAsync(scenario, scope, token)));

    /// <summary>Cancelling a queued Save cook preserves the prior material until an explicit retry.</summary>
    /// <returns>The cancellation and recovery regression.</returns>
    [TestMethod]
    public Task CancelledWorkspaceCookRetainsMaterialUntilRetry()
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, CheckCancelledWorkspaceCookAsync));

    private static async Task CheckQueuedPublicationMutationAsync(PublicationScenario scenario, string action, CancellationToken cancellationToken)
    {
        var fixture = scenario.Fixture;
        var nodes = fixture.Source.RootNodes.ToArray();
        var prior = scenario.Services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        scenario.Services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        if (string.Equals(action, "Undo", StringComparison.Ordinal))
        {
            await fixture.Context.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        }
        else if (string.Equals(action, "Clear", StringComparison.Ordinal))
        {
            _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [nodes[0].Id], 0, newMaterialUri: null, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }
        else
        {
            var geometry = nodes[0].Components.OfType<GeometryComponent>().Single();
            _ = (await fixture.Commands.RemoveComponentAsync(fixture.Context, nodes[0].Id, geometry.Id).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        var expected = await WaitForNodeAsync(fixture, nodes[0].Id, value => !value.MaterialKeys.Contains(scenario.Key, StringComparer.Ordinal), cancellationToken).ConfigureAwait(true);
        _ = expected.MaterialKeys.Should().NotContain(scenario.Key);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        scenario.Services.Runs.IsAutomaticCookingPaused = false;
        _ = await WaitForPublicationRunAsync(scenario.Services, prior, cancellationToken).ConfigureAwait(true);
        var after = await fixture.ReadNodeAsync(nodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = after.GeometryKey.Should().Be(expected.GeometryKey);
        _ = after.MaterialKeys.Should().Equal(expected.MaterialKeys);
        _ = after.MaterialBaseColors.Should().Equal(expected.MaterialBaseColors);
        if (!string.Equals(action, "Undo", StringComparison.Ordinal))
        {
            var retained = await fixture.ReadNodeAsync(nodes[1].Id, cancellationToken).ConfigureAwait(true);
            _ = retained.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }

        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        if (string.Equals(action, "Undo", StringComparison.Ordinal))
        {
            await fixture.Context.History.RedoAsync(cancellationToken).ConfigureAwait(true);
            _ = await WaitForNodeAsync(fixture, nodes[0].Id, value => value.MaterialBaseColors.Length == 1 && value.MaterialBaseColors[0] == new Vector4(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        }
    }

    private static Task<ContentCookResult> CookPublicationScopeAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken)
        => scope switch
        {
            CookTargetKind.Asset => scenario.Services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken),
            CookTargetKind.Folder => scenario.Services.Pipeline.CookFolderAsync(new("asset:///Content/Materials"), cancellationToken),
            CookTargetKind.CurrentScene => scenario.Services.Pipeline.CookCurrentSceneAsync(new("asset:///Content/Scenes/" + Uri.EscapeDataString(scenario.Fixture.Source.Name) + ".oscene.json"), cancellationToken),
            _ => scenario.Services.Pipeline.CookProjectAsync(cancellationToken),
        };

    private static async Task CheckUnchangedWorkspaceCookAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken)
    {
        _ = (await scenario.Fixture.Commands.SaveSceneAsync(scenario.Fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = await CookPublicationScopeAsync(scenario, scope, cancellationToken).ConfigureAwait(true);
        var content = scenario.Fixture.Runtime.ContentStatus;
        var root = Path.Combine(scenario.Fixture.ProjectRoot, ".cooked");
        var files = Directory.GetFiles(root, "*", SearchOption.AllDirectories).ToDictionary(static path => path, File.GetLastWriteTimeUtc, StringComparer.OrdinalIgnoreCase);
        var result = await CookPublicationScopeAsync(scenario, scope, cancellationToken).ConfigureAwait(true);
        _ = result.IsUpToDate.Should().BeTrue();
        _ = result.CookedAssets.Should().BeEmpty();
        _ = result.ReusedAssets.Should().NotBeEmpty();
        _ = scenario.Fixture.Runtime.ContentStatus.Should().Be(content);
        _ = Directory.GetFiles(root, "*", SearchOption.AllDirectories).ToDictionary(static path => path, File.GetLastWriteTimeUtc, StringComparer.OrdinalIgnoreCase).Should().BeEquivalentTo(files);
        var state = await scenario.Fixture.ReadNodeAsync(scenario.Fixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = state.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
    }

    private static async Task CheckCancelledWorkspaceCookAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var services = scenario.Services;
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        var content = scenario.Fixture.Runtime.ContentStatus;
        services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        CookRunSnapshot? queued;
        while ((queued = services.Runs.Runs.FirstOrDefault(run => !prior.Contains(run.OperationId))) is null)
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        _ = queued.State.Should().Be(CookRunState.Queued);
        await services.Runs.CancelAsync(queued.OperationId).ConfigureAwait(true);
        while (!services.Runs.Runs.Single(run => run.OperationId == queued.OperationId).IsCompleted)
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        _ = services.Runs.Runs.Single(run => run.OperationId == queued.OperationId).State.Should().Be(CookRunState.Cancelled);
        _ = scenario.Fixture.Runtime.ContentStatus.Should().Be(content);
        var before = await scenario.Fixture.ReadNodeAsync(scenario.Fixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = before.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
        var retried = await services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = retried.IsPublished.Should().BeTrue();
        _ = retried.IsMounted.Should().BeTrue();
        var after = await scenario.Fixture.ReadNodeAsync(scenario.Fixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = after.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        _ = services.Runs.Runs.Should().HaveCount(prior.Count + 2);
    }
}
