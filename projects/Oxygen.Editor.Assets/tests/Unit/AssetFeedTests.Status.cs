// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Shared freshness and project-scoped refresh regressions.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Unavailable evidence is a neutral status, not a corruption or runtime failure.</summary>
    [TestMethod]
    public void UnknownOutputAvailabilityDoesNotClaimDamage()
    {
        var status = new AssetCookStatus(AssetPath.Parse("/Content/Material.omat"), AssetCookFreshness.Current,
            HasPublishedOutput: true, CookedOutputAvailability.Unknown, [], [], []);
        _ = AssetStatusText.GetText(status, activity: null).Should().Be("Status pending");
        _ = AssetStatusText.GetTone(status, activity: null).Should().Be("Neutral");
        _ = AssetStatusText.GetDescription(status, activity: null).Should().NotContain("damaged");
    }

    /// <summary>Browser and picker retain the exact same saved-output evidence.</summary>
    /// <param name="freshness">The input comparison result.</param>
    /// <param name="published">Whether a committed product exists.</param>
    /// <param name="verified">Whether its output is still intact.</param>
    /// <param name="expected">The projected cooked overlay.</param>
    /// <returns>The asynchronous projection regression.</returns>
    [TestMethod]
    [DataRow(AssetCookFreshness.Current, true, true, AssetState.Cooked)]
    [DataRow(AssetCookFreshness.OutOfDate, true, true, AssetState.Stale)]
    [DataRow(AssetCookFreshness.OutOfDate, true, false, AssetState.Missing)]
    [DataRow(AssetCookFreshness.NeedsCooking, false, false, null)]
    public async Task BrowserAndPickerUseTheSameCookFacts(AssetCookFreshness freshness, bool published, bool verified, AssetState? expected)
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var source = AssetPath.Parse("/Content/Materials/Red.omat.json");
        var catalog = new TestProjectAssetCatalog([new AssetRecord(source)]);
        var state = new AssetCookStatus(AssetPath.Parse("/Content/Materials/Red.omat"), freshness, published, verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [], [], []);
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]));
        using var provider = AssetFeeds.Create(catalog, workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        IReadOnlyList<AssetEntry> rows = [];
        IReadOnlyList<AssetEntry> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Single().CookStatus.Should().BeSameAs(state);
        _ = rows.Single().DerivedState.Should().Be(expected);
        var material = choices.Single(choice => choice.DescriptorPath == rows.Single().DescriptorPath);
        _ = material.CookStatus.Should().BeSameAs(state);
        _ = material.DerivedState.Should().Be(expected);
    }

    /// <summary>Already-cooked-only reference kinds do not enter the source-cook status reader.</summary>
    /// <returns>The asynchronous catalog projection regression.</returns>
    [TestMethod]
    public async Task TypedReferenceDescriptorsDoNotRequestSourceCookStatus()
    {
        using var workspace = new AssetWorkspace();
        const string relativePath = "Content/Scripts/Orbit.oscript.json";
        var descriptorPath = workspace.SourcePath(relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(descriptorPath)!);
        File.WriteAllText(descriptorPath, """{"name":"Orbit"}""");
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/" + relativePath))]);
        var reader = new DelegateStatusReader((_, _, _) => throw new InvalidOperationException("Typed references are not source-cookable."));
        using var provider = AssetFeeds.Create(
            catalog,
            workspace.ActivateProject(),
            new WorkspaceCookScopeProvider(workspace),
            cookStatus: reader);
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);

        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = rows.Should().ContainSingle().Which.Kind.Should().Be(AssetKind.Script);
    }

    /// <summary>Concurrent consumers share work; a catalog burst supersedes only the in-flight snapshot.</summary>
    /// <returns>The asynchronous burst and caller-cancellation regression.</returns>
    [TestMethod]
    public async Task CatalogBurstDiscardsOldRowsAndCoalescesConsumers()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        WriteMaterial(workspace.SourcePath("Content/Materials/Blue.omat.json"));
        var red = AssetPath.Parse("/Content/Materials/Red.omat.json");
        var blue = AssetPath.Parse("/Content/Materials/Blue.omat.json");
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
        using var provider = AssetFeeds.Create(catalog, workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        var snapshots = new List<IReadOnlyList<AssetEntry>>();
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(snapshots.Add), this.TestContext.CancellationToken);
        var refresh = provider.RefreshAsync(this.TestContext.CancellationToken);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancelled = new CancellationTokenSource();
        var cancelledWait = provider.RefreshAsync(cancelled.Token);
        await cancelled.CancelAsync().ConfigureAwait(false);
        _ = await ((Func<Task>)(() => cancelledWait)).Should().ThrowExactlyAsync<TaskCanceledException>().ConfigureAwait(false);
        var shared = provider.RefreshAsync(this.TestContext.CancellationToken);
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
        using var workspace = new AssetWorkspace();
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
        var context = workspace.ActivateProject();
        var catalog = new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]);
        using var provider = AssetFeeds.Create(catalog, context, new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        var refresh = provider.RefreshAsync(this.TestContext.CancellationToken);
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

    /// <summary>Publication admission errors leave authoring selectable and surface their diagnostic without reopening the failed head.</summary>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task UnavailablePublicationShowsAuthoredRowsWithDiagnostic(bool broken)
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var source = AssetPath.Parse("/Content/Materials/Red.omat.json");
        var identity = AssetPath.Parse("/Content/Materials/Red.omat");
        if (broken)
        {
            await File.WriteAllTextAsync(workspace.SourcePath("Content/Materials/Red.omat.json"), "{broken", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        const string failure = "The publication document does not match the selected digest.";
        var catalog = new TestProjectAssetCatalog([new AssetRecord(source)]) { PublicationError = failure };
        var reader = new DelegateStatusReader((_, _, _) => throw new InvalidOperationException("Must not reopen failed publication."));
        using var provider = AssetFeeds.Create(catalog, workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        var item = await provider.ResolveAsync(identity, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = item.Should().NotBeNull();
        _ = item!.IsSelectable.Should().Be(!broken);
        _ = item.PrimaryState.Should().Be(broken ? AssetState.Broken : AssetState.Descriptor);
        _ = item.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Unknown);
        _ = item.CookStatus.OutputAvailability.Should().Be(CookedOutputAvailability.Unknown);
        _ = item.CookedPath.Should().BeNull();
        _ = AssetStatusText.GetText(item.CookStatus, activity: null).Should().Be("Status unavailable");
        _ = AssetStatusText.GetDescription(item.CookStatus, activity: null).Should().Be(failure);
    }

    /// <summary>Awaitable shutdown drains a cancelled status read before its owner removes retained output.</summary>
    [TestMethod]
    public async Task AsyncDisposalWaitsForThePublicationReaderToDrain()
    {
        using var workspace = new AssetWorkspace();
        WriteMaterial(workspace.SourcePath("Content/Materials/Red.omat.json"));
        var source = AssetPath.Parse("/Content/Materials/Red.omat.json");
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var cancelled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reader = new DelegateStatusReader(async (_, _, token) =>
        {
            entered.SetResult();
            try
            {
                await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
            }
            finally
            {
                cancelled.SetResult();
                await release.Task.ConfigureAwait(false);
            }

            return [];
        });
        var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new(source)]), workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        var refresh = provider.RefreshAsync(this.TestContext.CancellationToken);
        var disposal = Task.CompletedTask;
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            disposal = provider.DisposeAsync().AsTask();
            await cancelled.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = disposal.IsCompleted.Should().BeFalse("the status reader still owns its publication");
        }
        finally
        {
            release.TrySetResult();
            await disposal.ConfigureAwait(false);
            await provider.DisposeAsync().ConfigureAwait(false);
        }

        _ = await ((Func<Task>)(() => refresh)).Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    private sealed class DelegateStatusReader(Func<ProjectContext, IReadOnlyList<AssetPath>, CancellationToken, Task<IReadOnlyList<AssetCookStatus>>> read) : IAssetCookStatusReader
    {
        public event EventHandler? Changed;

        public void NotifyChanged() => this.Changed?.Invoke(this, EventArgs.Empty);

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReadLease publication, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
            => this.ReadAsync(project, assetUris, cancellationToken);

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
            => read(project, assetUris, cancellationToken);
    }
}
