// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Verifies publication after scene replacement and saved-source conflicts.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Closing the consuming scene while a cook waits cannot transfer its assignments to the next scene.</summary>
    /// <returns>The scene-lifetime integration regression.</returns>
    [TestMethod]
    public Task QueuedWorkspaceCookCannotReviveClosedSceneBindings()
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, CheckPublicationAfterSceneSwitchAsync));

    /// <summary>A changed source baseline fails cooking while the prior native material remains usable.</summary>
    /// <returns>The source-conflict recovery integration regression.</returns>
    [TestMethod]
    public Task WorkspaceSourceConflictPreservesPublishedMaterialUntilRecovery()
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, CheckPublicationSourceConflictAsync));

    private static async Task CheckPublicationAfterSceneSwitchAsync(PublicationScenario scenario, CancellationToken cancellationToken)
    {
        var fixture = scenario.Fixture;
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
        var content = scenario.Fixture.Runtime.ContentStatus;
        await File.WriteAllTextAsync(material.SourcePath, "{}", cancellationToken).ConfigureAwait(true);
        var failed = await scenario.Services.Pipeline.CookAssetAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = failed.Status.Should().Be(OperationStatus.Failed);
        _ = failed.IsPublished.Should().BeFalse();
        _ = failed.Diagnostics.Should().Contain(issue => issue.Severity == DiagnosticSeverity.Error);
        _ = scenario.Fixture.Runtime.ContentStatus.Should().Be(content);
        foreach (var node in scenario.Fixture.Source.RootNodes)
        {
            var retained = await scenario.Fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = retained.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(1, 0, 0, 1));
        }

        await File.WriteAllBytesAsync(material.SourcePath, original, cancellationToken).ConfigureAwait(true);
        scenario.Services.Runs.IsAutomaticCookingPaused = true;
        await SetPublicationMaterialColorAsync(scenario.Materials, material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
        var recovered = await scenario.Services.Pipeline.CookAssetAsync(material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = recovered.IsPublished.Should().BeTrue();
        _ = recovered.IsMounted.Should().BeTrue();
        foreach (var node in scenario.Fixture.Source.RootNodes)
        {
            var updated = await scenario.Fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = updated.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }
    }
}
