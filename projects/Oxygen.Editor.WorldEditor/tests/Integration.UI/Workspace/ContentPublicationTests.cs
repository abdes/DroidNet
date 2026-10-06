// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.PublicationWorkflows;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class ContentPublicationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task RenamingPublishedSceneKeepsPreviewAndNextSceneUsable() => EnqueueAsync(() =>
        CheckWorkspacePublicationAsync(this.TestContext, scope: null, async (scenario, token) =>
        {
            var fixture = scenario.SceneAuthoringFixture;
            _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
            var oldName = fixture.Source.Name;
            var oldUri = new Uri($"asset:///Content/Scenes/{Uri.EscapeDataString(oldName)}.oscene.json");
            _ = (await scenario.Services.Pipeline.CookCurrentSceneAsync(oldUri, token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
            _ = (await fixture.Commands.RenameSceneAsync(fixture.Context, "Small Scene").ConfigureAwait(true)).Succeeded.Should().BeTrue();
            var result = await scenario.Services.Pipeline.CookAssetAsync(new("asset:///Content/Scenes/Small%20Scene.oscene.json"), token).ConfigureAwait(true);
            _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
            _ = result.IsMounted.Should().BeTrue();
            _ = fixture.Runtime.ContentStatus.State.Should().Be(RuntimeContentState.Mounted);
            using (var publication = await scenario.Services.Publication.AcquireForMountAsync(scenario.Services.Projects.ActiveProject!, token).ConfigureAwait(true))
            {
                _ = publication.ProjectOutputPaths.Should().Contain("/Content/Scenes/Small_Scene.oscene")
                    .And.NotContain($"/Content/Scenes/{ContentPipelinePaths.NormalizeSceneDescriptorName(oldName)}.oscene");
            }

            foreach (var node in fixture.Source.RootNodes)
            {
                _ = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1, token).ConfigureAwait(true);
            }

            await fixture.SaveAndReopenAsync(token).ConfigureAwait(true);
            await fixture.SwitchToNewSceneAsync(cascades: 2, token).ConfigureAwait(true);
            await ObserveRenderedFramesAsync(fixture, token).ConfigureAwait(true);
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.Running);
        }));

    /// <summary>Closing a project cancels its cook, releases native readers and preserves the prior publication for reopening.</summary>
    /// <param name="captured">Whether native output has completed and preview capture is pending.</param>
    /// <returns>The project-lifetime integration check.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ClosingProjectCancelsCookAndReopensPriorNativePublication(bool captured) => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null, (scenario, token) => CheckProjectClosureAsync(scenario, captured, token)));

    private static async Task CheckProjectClosureAsync(PublicationScenario scenario, bool captured, CancellationToken cancellationToken)
    {
        var fixture = scenario.SceneAuthoringFixture;
        var services = scenario.Services;
        var project = services.Projects.ActiveProject!;
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var hashes = ReadPublishedHashes(fixture.ProjectRoot);
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        var runId = fixture.Runtime.ContentStatus.RunId;
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var registration = services.Publication.RegisterPreview(project, async () =>
        {
            var preview = await fixture.CreateWorkspacePreviewAsync(services, scenario.Catalog, project).ConfigureAwait(false);
            _ = entered.TrySetResult();
            await release.Task.ConfigureAwait(false);
            return preview;
        });
        try
        {
            services.Runs.IsAutomaticCookingPaused = true;
            await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
            _ = await WaitForProjectCookAsync(services, prior, completed: false, cancellationToken).ConfigureAwait(true);
            if (captured)
            {
                services.Runs.IsAutomaticCookingPaused = false;
                await entered.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
            }

            services.Projects.Close();
            registration.Dispose();
            await fixture.Runtime.ShutdownAsync().AsTask().WaitAsync(cancellationToken).ConfigureAwait(true);
            _ = release.TrySetResult();
            var cancelled = await WaitForProjectCookAsync(services, prior, completed: true, cancellationToken).ConfigureAwait(true);
            _ = cancelled.State.Should().Be(CookRunState.Cancelled);
            _ = services.Projects.ActiveProject.Should().BeNull();
            await scenario.Picker.RefreshAsync(MaterialPickerFilter.Default, cancellationToken).ConfigureAwait(true);
            _ = ReadPublishedHashes(fixture.ProjectRoot).Should().BeEquivalentTo(hashes);
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.NoEngine);
            using (await CookOutputLease.AcquireWriteAsync(fixture.ProjectRoot, CancellationToken.None).ConfigureAwait(true))
            {
            }

            await ReopenCancelledProjectAsync(scenario, project, runId, cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            _ = release.TrySetResult();
        }
    }

    private static async Task<CookRunSnapshot> WaitForProjectCookAsync(CatalogWorkloadServices services, HashSet<Guid> prior, bool completed, CancellationToken cancellationToken)
    {
        while (true)
        {
            if (services.Runs.Runs.FirstOrDefault(run => !prior.Contains(run.OperationId) && run.IsCompleted == completed) is { } run)
            {
                return run;
            }

            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }

    private static async Task ReopenCancelledProjectAsync(PublicationScenario scenario, ProjectContext project, Guid previousRunId, CancellationToken cancellationToken)
    {
        var fixture = scenario.SceneAuthoringFixture;
        var services = scenario.Services;
        services.Projects.Activate(project);
        using var registration = fixture.RegisterWorkspacePublication(services, scenario.Catalog);
        await fixture.InitializeAsync(cancellationToken).ConfigureAwait(true);
        using var reader = await services.Publication.AcquireForMountAsync(project, cancellationToken).ConfigureAwait(true);
        var bindings = reader.Roots.Zip(reader.RootPaths, static (root, path) => new RuntimeCookedRoot(path, root.Owner == CookPublicationRootOwner.Project ? root.Name : null)).ToArray();
        var mounts = await services.Mounts.PrepareAsync(project, reader, cancellationToken).ConfigureAwait(true);
        await fixture.Runtime.RefreshProjectCookedRootsAsync(bindings, mounts).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(cancellationToken).ConfigureAwait(true);
        _ = fixture.Runtime.ContentStatus.RunId.Should().NotBe(previousRunId);
        foreach (var node in fixture.Source.RootNodes)
        {
            _ = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1 && value.MaterialBaseColors[0] == new Vector4(1, 0, 0, 1), cancellationToken).ConfigureAwait(true);
        }

        var result = await services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue();
        _ = result.IsMounted.Should().BeTrue();
        foreach (var node in fixture.Source.RootNodes)
        {
            var updated = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = updated.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }
    }

    /// <summary>Every explicit scope publishes a saved material to existing native consumers.</summary>
    /// <param name="scope">The user-requested cook scope.</param>
    /// <returns>The end-to-end publication check.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Project)]
    public Task WorkspacePublicationRefreshesSharedMaterial(CookTargetKind scope) => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope));

    /// <summary>Saving a material uses automatic cooking and preserves unsaved scene assignments.</summary>
    /// <returns>The automatic publication check.</returns>
    [TestMethod]
    public Task WorkspaceAutomaticPublicationPreservesUnsavedScene() => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null));

    /// <summary>Publication cannot restore a binding that was undone, cleared or removed while queued.</summary>
    /// <param name="action">The later authored change.</param>
    /// <returns>The native lifetime regression.</returns>
    [TestMethod]
    [DataRow("Undo")]
    [DataRow("Clear")]
    [DataRow("RemoveGeometry")]
    public Task QueuedWorkspacePublicationKeepsLatestAssignment(string action) => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null, (scenario, token) => CheckQueuedPublicationMutationAsync(scenario, action, token)));

    /// <summary>Repeating unchanged work preserves published files and leaves the native preview untouched.</summary>
    /// <param name="scope">The user-requested scope.</param>
    /// <returns>The native incremental-reuse regression.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.Folder)]
    [DataRow(CookTargetKind.CurrentScene)]
    [DataRow(CookTargetKind.Project)]
    public Task UnchangedWorkspaceCookDoesNotRefreshNativeBindings(CookTargetKind scope) => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope, (scenario, token) => CheckUnchangedWorkspaceCookAsync(scenario, scope, token)));

    /// <summary>Cancelling a queued Save cook preserves the prior material until an explicit retry.</summary>
    /// <returns>The cancellation and recovery regression.</returns>
    [TestMethod]
    public Task CancelledWorkspaceCookRetainsMaterialUntilRetry() => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null, CheckCancelledWorkspaceCookAsync));

    private static async Task CheckQueuedPublicationMutationAsync(PublicationScenario scenario, string action, CancellationToken cancellationToken)
    {
        var fixture = scenario.SceneAuthoringFixture;
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
            _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [nodes[0].Id], await fixture.ReadSingleMaterialSlotAsync(nodes[0].Id, cancellationToken).ConfigureAwait(true), newMaterialUri: null, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
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

    private static Task<ContentCookResult> CookPublicationScopeAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken) => scope switch
    {
        CookTargetKind.Asset => scenario.Services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken),
        CookTargetKind.Folder => scenario.Services.Pipeline.CookFolderAsync(new("asset:///Content/Materials"), cancellationToken),
        CookTargetKind.CurrentScene => scenario.Services.Pipeline.CookCurrentSceneAsync(new("asset:///Content/Scenes/" + Uri.EscapeDataString(scenario.SceneAuthoringFixture.Source.Name) + ".oscene.json"), cancellationToken),
        _ => scenario.Services.Pipeline.CookProjectAsync(cancellationToken),
    };

    private static async Task CheckUnchangedWorkspaceCookAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken)
    {
        _ = (await scenario.SceneAuthoringFixture.Commands.SaveSceneAsync(scenario.SceneAuthoringFixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = await CookPublicationScopeAsync(scenario, scope, cancellationToken).ConfigureAwait(true);
        var content = scenario.SceneAuthoringFixture.Runtime.ContentStatus;
        var files = ReadPublishedHashes(scenario.SceneAuthoringFixture.ProjectRoot).Keys.ToDictionary(static path => path, File.GetLastWriteTimeUtc, StringComparer.OrdinalIgnoreCase);
        var result = await CookPublicationScopeAsync(scenario, scope, cancellationToken).ConfigureAwait(true);
        _ = result.IsUpToDate.Should().BeTrue();
        _ = result.CookedAssets.Should().BeEmpty();
        _ = result.ReusedAssets.Should().NotBeEmpty();
        _ = scenario.SceneAuthoringFixture.Runtime.ContentStatus.Should().Be(content);
        _ = ReadPublishedHashes(scenario.SceneAuthoringFixture.ProjectRoot).Keys.ToDictionary(static path => path, File.GetLastWriteTimeUtc, StringComparer.OrdinalIgnoreCase).Should().BeEquivalentTo(files);
        var state = await scenario.SceneAuthoringFixture.ReadNodeAsync(scenario.SceneAuthoringFixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = state.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
    }

    private static async Task CheckCancelledWorkspaceCookAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var services = scenario.Services;
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        var content = scenario.SceneAuthoringFixture.Runtime.ContentStatus;
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
        _ = scenario.SceneAuthoringFixture.Runtime.ContentStatus.Should().Be(content);
        var before = await scenario.SceneAuthoringFixture.ReadNodeAsync(scenario.SceneAuthoringFixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = before.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
        var retried = await services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = retried.IsPublished.Should().BeTrue();
        _ = retried.IsMounted.Should().BeTrue();
        var after = await scenario.SceneAuthoringFixture.ReadNodeAsync(scenario.SceneAuthoringFixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = after.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        _ = services.Runs.Runs.Should().HaveCount(prior.Count + 2);
    }

    /// <summary>Closing the consuming scene while a cook waits cannot transfer its assignments to the next scene.</summary>
    /// <returns>The scene-lifetime integration regression.</returns>
    [TestMethod]
    public Task QueuedWorkspaceCookCannotReviveClosedSceneBindings() => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null, CheckPublicationAfterSceneSwitchAsync));

    /// <summary>A changed source baseline fails cooking while the prior native material remains usable.</summary>
    /// <returns>The source-conflict recovery integration regression.</returns>
    [TestMethod]
    public Task WorkspaceSourceConflictPreservesPublishedMaterialUntilRecovery() => EnqueueAsync(() => CheckWorkspacePublicationAsync(this.TestContext, scope: null, CheckPublicationSourceConflictAsync));

    private static async Task CheckPublicationAfterSceneSwitchAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var fixture = scenario.SceneAuthoringFixture;
        var oldNodes = fixture.Source.RootNodes.Select(static node => node.Id).ToArray();
        var prior = scenario.Services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        scenario.Services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        await fixture.SwitchToNewSceneAsync(1, cancellationToken).ConfigureAwait(true);
        var current = fixture.Source;
        var node = current.RootNodes[0];
        var before = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount > 0, cancellationToken).ConfigureAwait(true);
        _ = before.MaterialKeys.Should().NotContain(scenario.Key);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        scenario.Services.Runs.IsAutomaticCookingPaused = false;
        _ = await WaitForPublicationRunAsync(scenario.Services, prior, cancellationToken).ConfigureAwait(true);
        _ = fixture.Source.Should().BeSameAs(current);
        var after = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
        _ = after.MaterialKeys.Should().Equal(before.MaterialKeys);
        _ = after.MaterialBaseColors.Should().Equal(before.MaterialBaseColors);
        foreach (var oldNode in oldNodes)
        {
            _ = (await fixture.ReadNodeAsync(oldNode, cancellationToken).ConfigureAwait(true)).Exists.Should().BeFalse();
        }

        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Context.Metadata.IsDirty.Should().BeFalse();
    }

    private static async Task CheckPublicationSourceConflictAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var material = scenario.Material;
        var original = await File.ReadAllBytesAsync(material.SourcePath, cancellationToken).ConfigureAwait(true);
        var content = scenario.SceneAuthoringFixture.Runtime.ContentStatus;
        await File.WriteAllTextAsync(material.SourcePath, "{}", cancellationToken).ConfigureAwait(true);
        var failed = await scenario.Services.Pipeline.CookAssetAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = failed.Diagnostics.Should().Contain(issue => issue.Severity == DiagnosticSeverity.Error);
        _ = scenario.SceneAuthoringFixture.Runtime.ContentStatus.Should().Be(content);
        foreach (var node in scenario.SceneAuthoringFixture.Source.RootNodes)
        {
            var retained = await scenario.SceneAuthoringFixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = retained.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
        }

        await File.WriteAllBytesAsync(material.SourcePath, original, cancellationToken).ConfigureAwait(true);
        scenario.Services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(scenario.Materials, material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        var recovered = await scenario.Services.Pipeline.CookAssetAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = recovered.IsPublished.Should().BeTrue();
        _ = recovered.IsMounted.Should().BeTrue();
        foreach (var node in scenario.SceneAuthoringFixture.Source.RootNodes)
        {
            var updated = await scenario.SceneAuthoringFixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = updated.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }
    }
}
