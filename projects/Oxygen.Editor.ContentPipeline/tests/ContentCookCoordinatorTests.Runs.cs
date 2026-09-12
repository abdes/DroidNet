// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies scoped history, cancellation, and explicit save recovery.</summary>
public sealed partial class ContentCookCoordinatorTests
{
    /// <summary>Catalog compatibility failures retain actionable artifact details in the selected cook.</summary>
    /// <returns>The asynchronous operation test.</returns>
    [TestMethod]
    public async Task CompatibilityFailuresKeepArtifactDiagnosticsInCookHistory()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var diagnostic = new DiagnosticRecord
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.RuntimeDiscovery,
            Severity = DiagnosticSeverity.Error,
            Code = Oxygen.Managed.Core.Compatibility.NativeCompatibilityDiagnosticCodes.ArtifactMismatch,
            Message = "The import tool changed.",
            AffectedPath = "ImportTool.exe",
        };
        Func<Task> cook = () => coordinator.RunCookAsync<int>(
            new(CookTargetKind.Project, ScopeUri: null),
            (_, _) => Task.FromException<int>(new Oxygen.Managed.Core.Compatibility.NativeCompatibilityException([diagnostic])),
            CancellationToken.None);
        _ = await cook.Should().ThrowAsync<Oxygen.Managed.Core.Compatibility.NativeCompatibilityException>().ConfigureAwait(false);
        var run = coordinator.Runs.Single();
        _ = run.Diagnostics.Should().ContainSingle().Which.Should().Be(diagnostic with { OperationId = run.OperationId });
    }

    /// <summary>Represents an entire project with one run and ordered messages for all its work.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ProjectRunRetainsItsScopeAndOrderedTranscript()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var reveals = new List<Guid>();
        coordinator.RunChanged += (_, args) =>
        {
            if (args.Reveal)
            {
                reveals.Add(args.Run.OperationId);
            }
        };
        var result = await coordinator.RunCookAsync(
            new(CookTargetKind.Project, ScopeUri: null),
            (operation, _) =>
            {
                CookRunContext.Report(new("Cooking Main."));
                CookRunContext.Report(new("Cooking NewScene2."));
                return Task.FromResult(new ContentCookResult(operation.OperationId, CookTargetKind.Project, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
            },
            CancellationToken.None).ConfigureAwait(false);

        var run = coordinator.Runs.Single();
        _ = run.Request.TargetKind.Should().Be(CookTargetKind.Project);
        _ = run.OperationId.Should().Be(result.OperationId);
        _ = run.State.Should().Be(CookRunState.Succeeded);
        _ = run.Messages.Select(static message => message.Text).Should().ContainInOrder("Cooking Main.", "Cooking NewScene2.", "Cook complete.");
        _ = run.Messages.Select(static message => message.Sequence).Should().BeInAscendingOrder();
        _ = reveals.Should().Equal(run.OperationId, run.OperationId);
        _ = CookRunContext.Current.Should().BeNull();
    }

    /// <summary>Cancellation targets one selected request and reports a safe stop before the next writer starts.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CancelSelectedRunKeepsQueuedCookAndWaitsForDrain()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var stopped = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => stopped.Task, CancellationToken.None);
        var firstId = coordinator.Runs.Single().OperationId;
        var second = coordinator.RunCookAsync(new(CookTargetKind.Asset, new("asset:///Content/Materials/Blue.omat.json")), (_, _) => Task.FromResult(2), CancellationToken.None);

        await coordinator.CancelAsync(firstId).ConfigureAwait(false);
        _ = coordinator.Runs.Single(run => run.OperationId == firstId).State.Should().Be(CookRunState.Cancelling);
        _ = second.IsCompleted.Should().BeFalse();
        stopped.SetResult(1);
        Func<Task> cancelled = async () => _ = await first.ConfigureAwait(false);
        _ = await cancelled.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = (await second.ConfigureAwait(false)).Should().Be(2);
        _ = coordinator.Runs.Single(run => run.OperationId == firstId).State.Should().Be(CookRunState.Cancelled);
    }

    /// <summary>Automatic work keeps its messages without requesting UI activation or leaking into other scopes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task AutomaticRunKeepsMessagesScopedWithoutReveal()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var reveal = false;
        coordinator.RunChanged += (_, args) => reveal |= args.Reveal;
        _ = await coordinator.RunCookAsync(
            new(CookTargetKind.Asset, new("asset:///Content/Materials/Blue.omat.json"), IsAutomatic: true),
            (_, _) =>
            {
                CookRunContext.Report(new("Blue material output."));
                return Task.FromResult(1);
            },
            CancellationToken.None).ConfigureAwait(false);
        CookRunContext.Report(new("Unrelated editor output."));
        _ = reveal.Should().BeFalse();
        _ = coordinator.Runs.Single().Messages.Select(static message => message.Text).Should().Contain("Blue material output.").And.NotContain("Unrelated editor output.");
    }

    /// <summary>Retained worker termination failures cannot appear complete while work is still draining.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RunTerminationFailureRemainsActiveUntilDrain()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var drained = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var failure = new ContentPipelineTerminationException(new IOException("Termination failed."), drained.Task);
        var operation = coordinator.RunCookAsync<int>(new(CookTargetKind.Project, ScopeUri: null), (_, _) => Task.FromException<int>(failure), CancellationToken.None);
        Func<Task> failed = async () => _ = await operation.ConfigureAwait(false);
        _ = await failed.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = coordinator.Runs.Single().State.Should().Be(CookRunState.Cancelling);
        _ = coordinator.Runs.Single().IsCompleted.Should().BeFalse();
        drained.SetResult();
        _ = await coordinator.RunAsync((_, _) => Task.FromResult(1), CancellationToken.None).ConfigureAwait(false);
        _ = coordinator.Runs.Single().State.Should().Be(CookRunState.Failed);
    }

    /// <summary>Broken presentation subscribers cannot break the worker or its writer ownership.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task PresentationFailureCannotFailCook()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        coordinator.RunChanged += (_, _) => throw new InvalidOperationException("Broken UI observer.");
        _ = (await coordinator.RunCookAsync(new(CookTargetKind.Project, ScopeUri: null), (_, _) => Task.FromResult(42), CancellationToken.None).ConfigureAwait(false)).Should().Be(42);
        _ = coordinator.Runs.Single().State.Should().Be(CookRunState.Succeeded);
    }

    /// <summary>A blocked cook releases the writer and reads saved inputs again after explicit resume.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task NeedsSaveReleasesWriterAndRechecksInputsOnResume()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var dirty = true;
        var captures = 0;
        var document = new Snapshots.CookDocumentState(Guid.NewGuid(), Path.Combine(Path.GetTempPath(), "Main.oscene.json"), "Main", 2, 1, IsDirty: true, "saved");
        var waiting = coordinator.RunCookAsync(
            new(CookTargetKind.Project, ScopeUri: null),
            (_, _) =>
        {
            captures++;
            return dirty ? Task.FromException<int>(new CookInputsNeedSaveException([document])) : Task.FromResult(100);
        },
            CancellationToken.None);
        var run = coordinator.Runs.Single();
        _ = run.State.Should().Be(CookRunState.NeedsSave);
        _ = (await coordinator.RunCookAsync(new(CookTargetKind.Asset, new("asset:///Content/Other.omat.json")), (_, _) => Task.FromResult(7), CancellationToken.None).ConfigureAwait(false)).Should().Be(7);
        _ = waiting.IsCompleted.Should().BeFalse();
        dirty = false;
        _ = coordinator.ResumeAfterSave(run.OperationId).Should().BeTrue();
        _ = (await waiting.ConfigureAwait(false)).Should().Be(100);
        _ = captures.Should().Be(2);
        _ = coordinator.Runs.Single(item => item.OperationId == run.OperationId).UnsavedDocuments.Should().BeEmpty();
    }

    /// <summary>Preflight failures retain discovered assets and distinguish the failing asset from skipped work.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FailedPreflightKeepsAffectedAndSkippedAssets()
    {
        using var coordinator = CreateCoordinator(CreateContextService());
        var main = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var material = new Uri("asset:///Content/Materials/Blue.omat.json");
        _ = await coordinator.RunCookAsync(
            new(CookTargetKind.Project, ScopeUri: null),
            (operation, _) =>
        {
            CookRunContext.Report(new(Asset: new(main, ContentCookAssetKind.Scene, CookAssetState.Preparing)));
            CookRunContext.Report(new(Asset: new(material, ContentCookAssetKind.Material, CookAssetState.Preparing)));
            var issue = new DiagnosticRecord
            {
                OperationId = operation.OperationId, Code = "Test.InvalidInput", Message = "Aerial Start is invalid.",
                Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error, AffectedVirtualPath = main.AbsolutePath,
            };
            return Task.FromResult(new ContentCookResult(operation.OperationId, CookTargetKind.Project, OperationStatus.Failed, [issue], [], Inspection: null, Validation: null));
        },
            CancellationToken.None).ConfigureAwait(false);
        var assets = coordinator.Runs.Single().Assets;
        _ = assets[main].State.Should().Be(CookAssetState.Failed);
        _ = assets[material].State.Should().Be(CookAssetState.Skipped);
    }
}
