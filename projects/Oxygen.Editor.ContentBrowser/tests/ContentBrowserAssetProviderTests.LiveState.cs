// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Live authoring and cook progress share one projection without rescanning on every event.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>Dirty state, cancellation and close update both surfaces without disk freshness work.</summary>
    /// <returns>The asynchronous live-document regression.</returns>
    [TestMethod]
    public async Task DocumentChangesUpdateBrowserAndPickerWithoutRescanning()
    {
        using var workspace = new TempWorkspace();
        var path = workspace.SourcePath("Content/Materials/Red.omat.json");
        WriteMaterial(path);
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        var catalog = new TestProjectAssetCatalog([new AssetRecord(uri)]);
        var documents = new CookDocumentRegistry();
        using var owner = documents.Register(path, _ => throw new InvalidOperationException("Presentation must not acquire the source."));
        var initial = new CookDocumentState(Guid.NewGuid(), path, "Red", 1, 1, IsDirty: false, new string('A', 64));
        owner.UpdateState(initial);
        var reads = 0;
        var sourceHash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)));
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], []) { SourcePaths = [path], SavedSourceHash = sourceHash }]);
        });
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, documents, EmptyCookRuns(), unavailableRuntime);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        IReadOnlyList<MaterialPickerResult> choices = [];
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value),
            this.TestContext.CancellationToken);
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        var preview = choices.Single(choice => choice.MaterialUri == uri).BaseColorPreview;
        _ = preview.Should().NotBeNull();
        var lockedSource = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None);
        await using var lockedSourceLifetime = lockedSource.ConfigureAwait(false);
        owner.UpdateState(initial with { IsDirty = true });
        _ = rows.Single().PrimaryBadge.Should().Be("Unsaved changes");
        _ = choices.Single(choice => choice.MaterialUri == uri).StatusText.Should().Be(rows.Single().PrimaryBadge);
        _ = rows.Single().CookStatus!.HasVerifiedOutput.Should().BeTrue();
        owner.UpdateState(initial);
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        owner.UpdateState(initial with { IsDirty = true });
        owner.Dispose();
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        _ = choices.Single(choice => choice.MaterialUri == uri).StatusText.Should().Be("Cooked");
        _ = choices.Single(choice => choice.MaterialUri == uri).BaseColorPreview.Should().Be(preview);
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
        using var workspace = new TempWorkspace();
        var red = new Uri("asset:///Content/Materials/Red.omat.json");
        var blue = new Uri("asset:///Content/Other/Blue.omat.json");
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Other/Blue.omat.json"));
        var catalog = new TestProjectAssetCatalog([new AssetRecord(red), new AssetRecord(blue)]);
        var context = CreateProjectContextService(workspace);
        IReadOnlyList<CookRunSnapshot> runs = [];
        var cookService = new Mock<ICookRunService>();
        _ = cookService.SetupGet(service => service.Runs).Returns(() => runs);
        var reads = 0;
        var reader = new DelegateStatusReader((_, uris, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>(uris.Select(uri => new AssetCookStatus(uri, AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], [])).ToArray());
        });
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, context, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), cookService.Object, unavailableRuntime);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        var notifications = 0;
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value =>
        {
            rows = value;
            notifications++;
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        var run = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(), ProjectId = context.ActiveProject!.ProjectId, ProjectRoot = workspace.Root, DisplayName = "Cook",
            Request = new(kind, kind is CookTargetKind.Asset or CookTargetKind.CurrentScene ? red : kind == CookTargetKind.Folder ? new Uri("asset:///Content/Materials") : null),
        };
        runs = [run];
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Count(row => string.Equals(row.PrimaryBadge, "Queued", StringComparison.Ordinal)).Should().Be(affected);
        var queuedNotifications = notifications;
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run with { Revision = 2 }));
        _ = notifications.Should().Be(queuedNotifications);
        run = run with { State = CookRunState.Cooking, Revision = 3 };
        runs = [run];
        cookService.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Count(row => string.Equals(row.PrimaryBadge, "Cooking", StringComparison.Ordinal)).Should().Be(affected);
        _ = rows.Should().OnlyContain(row => row.CookStatus!.HasVerifiedOutput);
        _ = reads.Should().Be(1);
    }

    /// <summary>A save invalidates Current immediately, before its asynchronous fingerprint check completes.</summary>
    /// <returns>The asynchronous saved-source transition regression.</returns>
    [TestMethod]
    public async Task SavedBytesInvalidateCurrentBeforeTheRefreshFinishes()
    {
        using var workspace = new TempWorkspace();
        var path = workspace.SourcePath("Content/Materials/Red.omat.json");
        WriteMaterial(path);
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        var documents = new CookDocumentRegistry();
        using var owner = documents.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        var original = new CookDocumentState(Guid.NewGuid(), path, "Red", 1, 1, IsDirty: false, "old");
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

            return [new(uri, changed ? AssetCookFreshness.OutOfDate : AssetCookFreshness.Current, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], []) { SourcePaths = [path], SavedSourceHash = changed ? "new" : "old" }];
        });
        var catalog = new TestProjectAssetCatalog([new AssetRecord(uri)]);
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, documents, EmptyCookRuns(), unavailableRuntime);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value =>
        {
            rows = value;
            if (value.Count == 1 && string.Equals(value[0].CookStatus?.SavedSourceHash, "new", StringComparison.Ordinal))
            {
                _ = published.TrySetResult();
            }
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        owner.UpdateState(original with { Revision = 2, SavedRevision = 2, SavedContentHash = "new" });
        _ = rows.Single().PrimaryBadge.Should().Be("Out of date");
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        release.SetResult();
        await published.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reads.Should().Be(2);
        _ = rows.Single().CookStatus!.HasVerifiedOutput.Should().BeTrue();
    }

    /// <summary>Completed cooks refresh output evidence once, including successful automatic cooks.</summary>
    /// <returns>The asynchronous completion refresh regression.</returns>
    [TestMethod]
    public async Task CookCompletionRefreshesEvidenceWithoutManualRefresh()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        var context = CreateProjectContextService(workspace);
        var run = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(), ProjectId = context.ActiveProject!.ProjectId, ProjectRoot = workspace.Root, DisplayName = "Red",
            Request = new(CookTargetKind.Asset, uri, IsAutomatic: true), State = CookRunState.Cooking,
        };
        var cooks = new Mock<ICookRunService>();
        _ = cooks.SetupGet(service => service.Runs).Returns(() => new[] { run });
        var refreshed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            var complete = Interlocked.Increment(ref reads) > 1;
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(uri, complete ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate, HasPublishedOutput: true, HasVerifiedOutput: true, [], [], [])]);
        });
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([new AssetRecord(uri)]), context, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), cooks.Object, unavailableRuntime);
        provider.Items.Subscribe(
            new Observer<IReadOnlyList<ContentBrowserAssetItem>>(items =>
        {
            if (items.Count == 1 && string.Equals(items[0].PrimaryBadge, "Cooked", StringComparison.Ordinal))
            {
                _ = refreshed.TrySetResult();
            }
        }),
            this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        run = run with { State = CookRunState.Succeeded, CompletedAt = DateTimeOffset.UtcNow };
        cooks.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run));
        await refreshed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        cooks.Raise(service => service.RunChanged += null, new CookRunChangedEventArgs(run, reveal: true));
        _ = reads.Should().Be(2);
    }
}
