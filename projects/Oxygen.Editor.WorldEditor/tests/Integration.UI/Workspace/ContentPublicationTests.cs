// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
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
            _ = (await fixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
            var oldName = fixture.Source.Name;
            var oldUri = new Uri($"asset:///Content/Scenes/{Uri.EscapeDataString(oldName)}.oscene.json");
            _ = (await scenario.Services.Pipeline.CookCurrentSceneAsync(oldUri, token).ConfigureAwait(true)).IsPublished.Should().BeTrue();
            using (var relocation = new AssetRelocationService(scenario.Services.Runs, scenario.Services.Projects, scenario.Services.Documents, new NativeAtomicFileStore(new RealFileSystem()), scenario.Catalog))
            using (fixture.FollowRelocations(relocation))
            {
                var move = new AssetRelocationMove("/Content/Scenes/" + oldName + ".oscene.json", "/Content/Scenes/Small Scene.oscene.json");
                _ = await relocation.RelocateAsync(new AssetRelocationRequest { Moves = [move] }, "Rename", new HistoryKeeper(fixture), token).ConfigureAwait(true);
            }

            _ = fixture.Source.Name.Should().Be("Small Scene");
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

    private static async Task CheckQueuedPublicationMutationAsync(PublicationScenario scenario, string action, CancellationToken cancellationToken)
    {
        var fixture = scenario.SceneAuthoringFixture;
        var nodes = fixture.Source.RootNodes.ToArray();
        var prior = scenario.Services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        var releaseWriter = QueueCookOnChange(scenario.Services, cancellationToken);
        await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        if (string.Equals(action, "Undo", StringComparison.Ordinal))
        {
            await fixture.Document.History.UndoAsync(cancellationToken).ConfigureAwait(true);
        }
        else if (string.Equals(action, "Clear", StringComparison.Ordinal))
        {
            _ = (await fixture.Authoring.ExecuteAsync(new SetMaterialSlot([nodes[0].Id], await fixture.ReadSingleMaterialSlotAsync(nodes[0].Id, cancellationToken).ConfigureAwait(true), material: null)).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }
        else
        {
            var geometry = nodes[0].Components.OfType<GeometryComponent>().Single();
            _ = (await fixture.Authoring.ExecuteAsync(new RemoveComponent(nodes[0].Id, geometry.Id)).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        }

        var expected = await WaitForNodeAsync(fixture, nodes[0].Id, value => !value.MaterialKeys.Contains(scenario.Key, StringComparer.Ordinal), cancellationToken).ConfigureAwait(true);
        _ = expected.MaterialKeys.Should().NotContain(scenario.Key);
        var revision = fixture.Document.History.Position;
        var history = fixture.Document.History.UndoStack.Count;
        await releaseWriter().ConfigureAwait(true);
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

        _ = fixture.Document.History.Position.Should().Be(revision);
        _ = fixture.Document.History.UndoStack.Should().HaveCount(history);
        if (string.Equals(action, "Undo", StringComparison.Ordinal))
        {
            await fixture.Document.History.RedoAsync(cancellationToken).ConfigureAwait(true);
            _ = await WaitForNodeAsync(fixture, nodes[0].Id, value => value.MaterialBaseColors.Length == 1 && value.MaterialBaseColors[0] == new Vector4(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        }
    }

    private static Task<ContentCookResult> CookPublicationScopeAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken) => scope switch
    {
        CookTargetKind.Asset => scenario.Services.Pipeline.CookAssetAsync(scenario.Material.File.AssetUri, cancellationToken),
        CookTargetKind.Folder => scenario.Services.Pipeline.CookFolderAsync(new("asset:///Content/Materials"), cancellationToken),
        CookTargetKind.CurrentScene => scenario.Services.Pipeline.CookCurrentSceneAsync(new("asset:///Content/Scenes/" + Uri.EscapeDataString(scenario.SceneAuthoringFixture.Source.Name) + ".oscene.json"), cancellationToken),
        _ => scenario.Services.Pipeline.CookProjectAsync(cancellationToken),
    };

    private static async Task CheckUnchangedWorkspaceCookAsync(PublicationScenario scenario, CookTargetKind scope, CancellationToken cancellationToken)
    {
        _ = (await scenario.SceneAuthoringFixture.Document.SaveAsync().ConfigureAwait(true)).Succeeded.Should().BeTrue();
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
        var releaseWriter = QueueCookOnChange(scenario.Services, cancellationToken);
        await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        await fixture.SwitchToNewSceneAsync(1, cancellationToken).ConfigureAwait(true);
        var current = fixture.Source;
        var node = current.RootNodes[0];
        var before = await WaitForNodeAsync(fixture, node.Id, static value => value.IndexCount > 0, cancellationToken).ConfigureAwait(true);
        _ = before.MaterialKeys.Should().NotContain(scenario.Key);
        var revision = fixture.Document.History.Position;
        var history = fixture.Document.History.UndoStack.Count;
        await releaseWriter().ConfigureAwait(true);
        _ = await WaitForPublicationRunAsync(scenario.Services, prior, cancellationToken).ConfigureAwait(true);
        _ = fixture.Source.Should().BeSameAs(current);
        var after = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
        _ = after.MaterialKeys.Should().Equal(before.MaterialKeys);
        _ = after.MaterialBaseColors.Should().Equal(before.MaterialBaseColors);
        foreach (var oldNode in oldNodes)
        {
            _ = (await fixture.ReadNodeAsync(oldNode, cancellationToken).ConfigureAwait(true)).Exists.Should().BeFalse();
        }

        _ = fixture.Document.History.Position.Should().Be(revision);
        _ = fixture.Document.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Document.IsDirty.Should().BeFalse();
    }

    private static async Task CheckPublicationSourceConflictAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var material = scenario.Material;
        var original = await File.ReadAllBytesAsync(material.File.Path, cancellationToken).ConfigureAwait(true);
        var content = scenario.SceneAuthoringFixture.Runtime.ContentStatus;
        await File.WriteAllTextAsync(material.File.Path, "{}", cancellationToken).ConfigureAwait(true);
        var failed = await scenario.Services.Pipeline.CookAssetAsync(material.File.AssetUri, cancellationToken).ConfigureAwait(true);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = failed.Diagnostics.Should().Contain(issue => issue.Severity == DiagnosticSeverity.Error);
        _ = scenario.SceneAuthoringFixture.Runtime.ContentStatus.Should().Be(content);
        foreach (var node in scenario.SceneAuthoringFixture.Source.RootNodes)
        {
            var retained = await scenario.SceneAuthoringFixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = retained.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
        }

        await File.WriteAllBytesAsync(material.File.Path, original, cancellationToken).ConfigureAwait(true);
        await SetPublicationMaterialColorAsync(scenario.Materials, material, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        var recovered = await scenario.Services.Pipeline.CookAssetAsync(material.File.AssetUri, cancellationToken).ConfigureAwait(true);
        _ = recovered.IsPublished.Should().BeTrue();
        _ = recovered.IsMounted.Should().BeTrue();
        foreach (var node in scenario.SceneAuthoringFixture.Source.RootNodes)
        {
            var updated = await scenario.SceneAuthoringFixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = updated.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }
    }
}
