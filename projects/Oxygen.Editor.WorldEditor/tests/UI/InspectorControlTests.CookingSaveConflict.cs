// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World.Cooking;

namespace Oxygen.Editor.World.Tests;

/// <summary>Connects Cooking's named save action to real material save conflicts and recovery.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A failed named save preserves source and output; successful recovery resumes the same cook and clears feedback.</summary>
    /// <returns>The save-conflict UI/native integration journey.</returns>
    [TestMethod]
    public Task CookingSaveListedConflictKeepsCookBlockedUntilRecovery()
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, CheckCookingSaveConflictAsync));

    private static async Task CheckCookingSaveConflictAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var services = scenario.Services;
        var materials = scenario.Materials;
        services.Runs.IsAutomaticCookingPaused = true;
        var unrelated = await CreateDirtySaveParticipantsAsync(scenario, cancellationToken).ConfigureAwait(true);
        var actions = new Mock<ICookingWorkspaceActions>();
        _ = actions.Setup(value => value.SaveListedAsync(It.IsAny<CookRunSnapshot>())).Returns<CookRunSnapshot>(async run =>
        {
            _ = run.UnsavedDocuments.Should().ContainSingle().Which.DocumentId.Should().Be(scenario.Material.DocumentId);
            return (await materials.SaveAsync(scenario.Material.DocumentId, cancellationToken).ConfigureAwait(true)).Succeeded;
        });
        using var panel = new CookingPanelViewModel(services.Runs, services.Pipeline, services.Projects, actions.Object, CreateStatusHosting());
        var view = new CookingPanelView { ViewModel = panel };
        var root = new Grid { Width = 900, Height = 600, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Black), Children = { view } };
        await LoadTestContentAsync(root).ConfigureAwait(true);
        var cook = services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken);
        try
        {
            await WaitForBlockedMaterialAsync(panel, scenario.Material.DocumentId, cancellationToken).ConfigureAwait(true);
            var id = panel.SelectedRun!.Snapshot.OperationId;
            var published = ReadPublishedHashes(scenario.Fixture.ProjectRoot);
            var source = JsonNode.Parse(await File.ReadAllTextAsync(scenario.Material.SourcePath, cancellationToken).ConfigureAwait(true))!;
            source["PbrMetallicRoughness"]!["BaseColorFactor"] = new JsonArray(0f, 0f, 1f, 1f);
            var external = source.ToJsonString();
            await File.WriteAllTextAsync(scenario.Material.SourcePath, external, cancellationToken).ConfigureAwait(true);
            await InvokeCookingSaveAsync(panel, view, cancellationToken).ConfigureAwait(true);
            _ = panel.ActionError.Should().Contain("conflict");
            _ = panel.SelectedRun!.Snapshot.State.Should().Be(CookRunState.NeedsSave);
            _ = cook.IsCompleted.Should().BeFalse();
            _ = ReadPublishedHashes(scenario.Fixture.ProjectRoot).Should().BeEquivalentTo(published);
            _ = (await File.ReadAllTextAsync(scenario.Material.SourcePath, cancellationToken).ConfigureAwait(true)).Should().Be(external);
            _ = materials.GetDocument(scenario.Material.DocumentId).IsDirty.Should().BeTrue();
            _ = await materials.ReloadAsync(scenario.Material.DocumentId, cancellationToken).ConfigureAwait(true);
            await InvokeCookingSaveAsync(panel, view, cancellationToken).ConfigureAwait(true);
            var result = await cook.WaitAsync(cancellationToken).ConfigureAwait(true);
            await AssertRecoveredNamedSaveAsync(scenario, panel, unrelated.DocumentId, id, result, cancellationToken).ConfigureAwait(true);
            actions.Verify(value => value.SaveListedAsync(It.IsAny<CookRunSnapshot>()), Times.Exactly(2));
        }
        finally
        {
            await materials.CloseAsync(unrelated.DocumentId, discard: true, cancellationToken).ConfigureAwait(true);
            foreach (var run in services.Runs.Runs.Where(static run => !run.IsCompleted))
            {
                await services.Runs.CancelAsync(run.OperationId).ConfigureAwait(true);
            }

            await UnloadTestContentAsync(root).ConfigureAwait(true);
        }
    }

    private static async Task AssertRecoveredNamedSaveAsync(PublicationScenario scenario, CookingPanelViewModel panel, Guid unrelatedId, Guid operationId, ContentCookResult result, CancellationToken cancellationToken)
    {
        _ = result.IsPublished.Should().BeTrue();
        _ = result.OperationId.Should().Be(operationId);
        _ = panel.ActionError.Should().BeEmpty();
        _ = scenario.Materials.GetDocument(unrelatedId).IsDirty.Should().BeTrue();
        _ = scenario.Fixture.Context.Metadata.IsDirty.Should().BeTrue();
        var node = await scenario.Fixture.ReadNodeAsync(scenario.Fixture.Source.RootNodes[0].Id, cancellationToken).ConfigureAwait(true);
        _ = node.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 0, 1, 1));
    }

    private static async Task<MaterialDocument> CreateDirtySaveParticipantsAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var materials = scenario.Materials;
        var unrelated = await materials.CreateAsync(new("asset:///Content/Materials/Unrelated.omat.json"), cancellationToken).ConfigureAwait(true);
        _ = (await materials.EditScalarAsync(unrelated.DocumentId, new(MaterialFieldKeys.MetallicFactor, 0.7f), cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = (await materials.EditScalarAsync(scenario.Material.DocumentId, new(MaterialFieldKeys.BaseColorG, 1f), cancellationToken).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        return unrelated;
    }

    private static async Task WaitForBlockedMaterialAsync(CookingPanelViewModel panel, Guid documentId, CancellationToken cancellationToken)
    {
        while (panel.SelectedRun?.Snapshot is not { State: CookRunState.NeedsSave } run || !run.UnsavedDocuments.Any(document => document.DocumentId == documentId))
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }

    private static async Task InvokeCookingSaveAsync(CookingPanelViewModel panel, CookingPanelView view, CancellationToken cancellationToken)
    {
        await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
        InvokeImportButton(view.FindDescendant<Button>(button => string.Equals(button.Content as string, "Save listed & Cook", StringComparison.Ordinal))!);
        if (panel.SaveListedAndCookCommand.ExecutionTask is { } pending)
        {
            await pending.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
    }
}
