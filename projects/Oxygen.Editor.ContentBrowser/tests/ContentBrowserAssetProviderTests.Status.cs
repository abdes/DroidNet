// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Shared freshness and project-scoped refresh regressions.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Browser and picker retain the exact same saved-output evidence.</summary>
    /// <param name="freshness">The input comparison result.</param>
    /// <param name="published">Whether a committed product exists.</param>
    /// <param name="verified">Whether its output is still intact.</param>
    /// <param name="expected">The projected cooked overlay.</param>
    /// <returns>The asynchronous projection regression.</returns>
    [TestMethod]
    [DataRow(AssetCookFreshness.Current, true, true, AssetState.Cooked)]
    [DataRow(AssetCookFreshness.OutOfDate, true, true, AssetState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, false, AssetState.Broken)]
    [DataRow(AssetCookFreshness.NeedsCooking, false, false, null)]
    public async Task BrowserAndPickerUseTheSameCookFacts(AssetCookFreshness freshness, bool published, bool verified, AssetState? expected)
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var uri = new Uri("asset:///Content/Materials/Red.omat.json");
        var catalog = new TestProjectAssetCatalog([new AssetRecord(uri)]);
        var state = new AssetCookStatus(uri, freshness, published, verified, [], [], []);
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]));
        using var provider = new ContentBrowserAssetProvider(catalog, CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns());
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        IReadOnlyList<MaterialPickerResult> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Single().CookStatus.Should().BeSameAs(state);
        _ = rows.Single().DerivedState.Should().Be(expected);
        var material = choices.Single(choice => string.Equals(choice.DescriptorPath, rows.Single().DescriptorPath, StringComparison.Ordinal));
        _ = material.CookStatus.Should().BeSameAs(state);
        _ = material.DerivedState.Should().Be(expected);
    }

    /// <summary>Concurrent consumers share work; a catalog burst supersedes only the in-flight snapshot.</summary>
    /// <returns>The asynchronous burst and caller-cancellation regression.</returns>
    [TestMethod]
    public async Task CatalogBurstDiscardsOldRowsAndCoalescesConsumers()
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Materials/Blue.omat.json"));
        var red = new Uri("asset:///Content/Materials/Red.omat.json");
        var blue = new Uri("asset:///Content/Materials/Blue.omat.json");
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reads = 0;
        var reader = new DelegateStatusReader(async (_, _, token) =>
        {
            if (Interlocked.Increment(ref reads) == 1)
            {
                started.SetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(false);
            }

            return [];
        });
        var catalog = new TestProjectAssetCatalog([new AssetRecord(red)]);
        using var provider = new ContentBrowserAssetProvider(catalog, CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns());
        var snapshots = new List<IReadOnlyList<ContentBrowserAssetItem>>();
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(snapshots.Add), this.TestContext.CancellationToken);
        var refresh = provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancelled = new CancellationTokenSource();
        var cancelledWait = provider.RefreshAsync(AssetBrowserFilter.Default, cancelled.Token);
        await cancelled.CancelAsync().ConfigureAwait(false);
        _ = await ((Func<Task>)(() => cancelledWait)).Should().ThrowExactlyAsync<TaskCanceledException>().ConfigureAwait(false);
        var shared = provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken);
        catalog.SetRecords([new AssetRecord(red), new AssetRecord(blue)]);
        for (var index = 0; index < 100; index++)
        {
            catalog.Publish(new(AssetChangeKind.Added, blue));
        }

        release.SetResult();
        await Task.WhenAll(refresh, shared).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reads.Should().Be(2);
        _ = catalog.RefreshCount.Should().Be(2);
        _ = snapshots.Should().HaveCount(2);
        _ = snapshots[^1].Count.Should().Be(2);
    }

    /// <summary>Closing a project cancels its pending scan and prevents late old rows.</summary>
    /// <param name="dispose">Whether to dispose the provider instead of closing the project.</param>
    /// <returns>The asynchronous lifetime regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task ClosingWorkspaceCancelsPendingStatus(bool dispose)
    {
        using var workspace = new TempWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var drained = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reader = new DelegateStatusReader(async (_, _, token) =>
        {
            started.SetResult();
            try
            {
                await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
                return [];
            }
            finally
            {
                drained.SetResult();
            }
        });
        var context = CreateProjectContextService(workspace);
        var catalog = new TestProjectAssetCatalog([new AssetRecord(new Uri("asset:///Content/Materials/Red.omat.json"))]);
        using var provider = new ContentBrowserAssetProvider(catalog, context, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns());
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        var refresh = provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        if (dispose)
        {
            provider.Dispose();
            _ = await ((Func<Task>)(() => refresh)).Should().ThrowExactlyAsync<TaskCanceledException>().ConfigureAwait(false);
        }
        else
        {
            context.Close();
            await refresh.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        await drained.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().BeEmpty();
    }

    private static ICookRunService EmptyCookRuns() => Mock.Of<ICookRunService>(runs => runs.Runs == Array.Empty<CookRunSnapshot>());

    private sealed class DelegateStatusReader(Func<ProjectContext, IReadOnlyList<Uri>, CancellationToken, Task<IReadOnlyList<AssetCookStatus>>> read) : IAssetCookStatusReader
    {
        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<Uri> assetUris, CancellationToken cancellationToken = default)
            => read(project, assetUris, cancellationToken);
    }
}
