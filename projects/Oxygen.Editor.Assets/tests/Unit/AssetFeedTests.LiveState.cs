// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Live authoring and cook progress share one projection without rescanning on every event.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>Dirty state, cancellation and close update both surfaces without disk freshness work.</summary>
    /// <returns>The asynchronous live-document regression.</returns>
    [TestMethod]
    public async Task DocumentChangesUpdateBrowserAndPickerWithoutRescanning()
    {
        using var workspace = new AssetWorkspace();
        var path = FileSystemPath.From(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(path.FullPath);
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]);
        var documents = new CookDocumentRegistry();
        using var owner = documents.Register(path, _ => throw new InvalidOperationException("Presentation must not acquire the source."));
        var initial = new CookDocumentState(path, "Red", IsDirty: false, new string('A', 64));
        owner.UpdateState(initial);
        var reads = 0;
        var sourceHash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(path.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)));
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(identity, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], []) { SourcePaths = [path], SavedSourceHash = sourceHash }]);
        });
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        var projects = workspace.ActivateProject();
        var project = projects.ActiveProject!;
        using var provider = new AssetFeed(catalog, projects, new WorkspaceCookScopeProvider(workspace), reader, documents, EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
        IReadOnlyList<AssetEntry> rows = [];
        IReadOnlyList<AssetEntry> choices = [];
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<AssetEntry>>(value => rows = value),
            this.TestContext.CancellationToken);
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = MaterialPreviewColor.TryRead(project, choices.Single(choice => choice.Identity == identity));
        _ = preview.Should().NotBeNull();
        var lockedSource = new FileStream(path.FullPath, FileMode.Open, FileAccess.Read, FileShare.None);
        await using var lockedSourceLifetime = lockedSource.ConfigureAwait(false);
        owner.UpdateState(initial with { IsDirty = true });
        _ = rows.Single().PrimaryBadge.Should().Be("Unsaved changes");
        _ = AssetStatusText.ForEntry(choices.Single(choice => choice.Identity == identity)).Should().Be(rows.Single().PrimaryBadge);
        _ = rows.Single().CookStatus!.HasAvailableOutput.Should().BeTrue();
        owner.UpdateState(initial);
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        owner.UpdateState(initial with { IsDirty = true });
        owner.Dispose();
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        _ = AssetStatusText.ForEntry(choices.Single(choice => choice.Identity == identity)).Should().Be("Cooked");
        _ = MaterialPreviewColor.TryRead(project, choices.Single(choice => choice.Identity == identity)).Should().Be(preview);
        _ = reads.Should().Be(1);
    }

    /// <summary>Asset, folder and project progress affects the correct rows and leaves previous output intact.</summary>
    /// <param name="kind">The explicit cook scope.</param>
    /// <param name="affected">How many rows belong to that scope.</param>
    /// <returns>The asynchronous cook-progress regression.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset, 1)]
    [DataRow(CookTargetKind.CurrentScene, 1)]
    [DataRow(CookTargetKind.Folder, 1)]
    [DataRow(CookTargetKind.Project, 2)]
    public async Task CookProgressUpdatesItsScopeWithoutRepeatedFreshnessChecks(CookTargetKind kind, int affected)
    {
        using var workspace = new AssetWorkspace();
        var red = AssetPath.Parse("/Content/Materials/Red.omat.json");
        var blue = AssetPath.Parse("/Content/Other/Blue.omat.json");
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Other/Blue.omat.json"));
        var catalog = new TestProjectAssetCatalog([new AssetRecord(red), new AssetRecord(blue)]);
        var context = workspace.ActivateProject();
        IReadOnlyList<CookRunSnapshot> runs = [];
        var cookService = new Mock<ICookRunService>();
        _ = cookService.SetupGet(service => service.Runs).Returns(() => runs);
        var reads = 0;
        var reader = new DelegateStatusReader((_, uris, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>(uris.Select(uri => new AssetCookStatus(uri, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], [])).ToArray());
        });
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, context, new WorkspaceCookScopeProvider(workspace), reader, new CookDocumentRegistry(), cookService.Object, unavailableRuntime, NoRelocations(), NoAuthoring());
        IReadOnlyList<AssetEntry> rows = [];
        var notifications = 0;
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<AssetEntry>>(value =>
        {
            rows = value;
            notifications++;
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var run = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(),
            ProjectId = context.ActiveProject!.ProjectId,
            ProjectRoot = workspace.Root,
            DisplayName = "Cook",
            Request = new(kind, kind is CookTargetKind.Asset or CookTargetKind.CurrentScene ? AssetPath.Parse("/Content/Materials/Red.omat") : kind == CookTargetKind.Folder ? AssetPath.Parse("/Content/Materials") : null),
        };
        runs = [run];
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Count(row => string.Equals(row.PrimaryBadge, "Queued", StringComparison.Ordinal)).Should().Be(affected);
        var queuedNotifications = notifications;
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = notifications.Should().Be(queuedNotifications);
        run = run with { State = CookRunState.Cooking };
        runs = [run];
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Count(row => string.Equals(row.PrimaryBadge, "Cooking", StringComparison.Ordinal)).Should().Be(affected);
        _ = rows.Should().OnlyContain(row => row.CookStatus!.HasAvailableOutput);
        _ = reads.Should().Be(1);
    }

    /// <summary>A save invalidates Current immediately, before its asynchronous fingerprint check completes.</summary>
    /// <returns>The asynchronous saved-source transition regression.</returns>
    [TestMethod]
    public async Task SavedBytesInvalidateCurrentBeforeTheRefreshFinishes()
    {
        using var workspace = new AssetWorkspace();
        var path = FileSystemPath.From(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(path.FullPath);
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        var documents = new CookDocumentRegistry();
        using var owner = documents.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        var original = new CookDocumentState(path, "Red", IsDirty: false, "old");
        owner.UpdateState(original);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var published = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reads = 0;
        var reader = new DelegateStatusReader(async (_, _, token) =>
        {
            var changed = Interlocked.Increment(ref reads) > 1;
            if (changed)
            {
                started.SetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(false);
            }

            return [new(identity, changed ? AssetCookFreshness.OutOfDate : AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], []) { SourcePaths = [path], SavedSourceHash = changed ? "new" : "old" }];
        });
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]);
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), reader, documents, EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<AssetEntry>>(value =>
        {
            rows = value;
            if (value.Count == 1 && string.Equals(value[0].CookStatus?.SavedSourceHash, "new", StringComparison.Ordinal))
            {
                _ = published.TrySetResult();
            }
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        owner.UpdateState(original with { SavedContentHash = "new" });
        _ = rows.Single().PrimaryBadge.Should().Be("Out of date");
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        release.SetResult();
        await published.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reads.Should().Be(2);
        _ = rows.Single().CookStatus!.HasAvailableOutput.Should().BeTrue();
    }

    /// <summary>Completed cooks refresh output evidence once, including successful automatic cooks.</summary>
    /// <returns>The asynchronous completion refresh regression.</returns>
    [TestMethod]
    public async Task CookCompletionRefreshesEvidenceWithoutManualRefresh()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        var context = workspace.ActivateProject();
        var run = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(),
            ProjectId = context.ActiveProject!.ProjectId,
            ProjectRoot = workspace.Root,
            DisplayName = "Red",
            Request = new(CookTargetKind.Asset, identity, IsAutomatic: true),
            State = CookRunState.Cooking,
        };
        var cooks = new Mock<ICookRunService>();
        _ = cooks.SetupGet(service => service.Runs).Returns(() => new[] { run });
        var refreshed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            var complete = Interlocked.Increment(ref reads) > 1;
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(identity, complete ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], [])]);
        });
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]), context, new WorkspaceCookScopeProvider(workspace), reader, new CookDocumentRegistry(), cooks.Object, unavailableRuntime, NoRelocations(), NoAuthoring());
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<AssetEntry>>(items =>
        {
            if (items.Count == 1 && string.Equals(items[0].PrimaryBadge, "Cooked", StringComparison.Ordinal))
            {
                _ = refreshed.TrySetResult();
            }
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        run = run with { State = CookRunState.Succeeded, CompletedAt = DateTimeOffset.UtcNow };
        cooks.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        await refreshed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        cooks.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run, reveal: true));
        _ = reads.Should().Be(2);
    }
}
