// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.Assets.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
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
        var unsaved = new OpenEdits(path, identity);
        var reads = 0;
        var sourceHash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(path.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)));
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(identity, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], []) { SourcePaths = [path], SavedSourceHash = sourceHash }]);
        });
        var projects = workspace.ActivateProject();
        var project = projects.ActiveProject!;
        using var provider = AssetFeeds.Create(catalog, projects, new WorkspaceCookScopeProvider(workspace), cookStatus: reader, documents: unsaved.Port.Object);
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
        unsaved.Edit();
        _ = rows.Single().CookStatus!.HasUnsavedChanges.Should().BeTrue();
        _ = rows.Single().PrimaryBadge.Should().Be("Unsaved changes");
        _ = AssetStatusText.ForEntry(choices.Single(choice => choice.Identity == identity)).Should().Be(rows.Single().PrimaryBadge);
        _ = rows.Single().CookStatus!.HasAvailableOutput.Should().BeTrue();
        unsaved.Clear();
        _ = rows.Single().CookStatus!.HasUnsavedChanges.Should().BeFalse();
        _ = rows.Single().PrimaryBadge.Should().Be("Cooked");
        unsaved.Edit();
        unsaved.Clear();
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
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>(uris.Select(uri => new AssetCookStatus(uri, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [])).ToArray());
        });
        using var provider = AssetFeeds.Create(catalog, context, new WorkspaceCookScopeProvider(workspace), cookStatus: reader, cooks: cookService.Object);
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
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([new(identity, complete ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [])]);
        });
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new AssetRecord(AssetPath.Parse("/Content/Materials/Red.omat.json"))]), context, new WorkspaceCookScopeProvider(workspace), cookStatus: reader, cooks: cooks.Object);
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

    /// <summary>The unsaved documents of one material source: an edit and its save change what <c>Find</c> reports.</summary>
    private sealed class OpenEdits
    {
        private readonly FileSystemPath path;
        private readonly UnsavedDocument document;
        private IReadOnlyList<UnsavedDocument> open = [];

        public OpenEdits(FileSystemPath path, AssetPath asset)
        {
            this.path = path;
            this.document = new UnsavedDocument(new DocumentId(Guid.NewGuid()), new DocumentFile(path, asset), "Red");
            _ = this.Port.Setup(value => value.Find(It.IsAny<IEnumerable<FileSystemPath>>()))
                .Returns((IEnumerable<FileSystemPath> paths) => paths.Contains(this.path) ? this.open : []);
        }

        public Mock<IUnsavedDocuments> Port { get; } = new();

        public void Edit() => this.Change([this.document]);

        public void Clear() => this.Change([]);

        private void Change(IReadOnlyList<UnsavedDocument> now)
        {
            this.open = now;
            this.Port.Raise(value => value.Changed += null, EventArgs.Empty);
        }
    }
}
