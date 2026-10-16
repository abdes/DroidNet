// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using System.Threading.Channels;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AutomaticCookServiceTests
{
    private static readonly Uri Main = new("asset:///Content/Scenes/Main.oscene.json");
    private static readonly Uri Wood = new("asset:///Content/Materials/Wood.omat.json");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Changes wait out the quiet window, which each change restarts, then each changed source cooks once.</summary>
    /// <returns>The asynchronous quiet-window regression.</returns>
    [TestMethod]
    public async Task ChangesSettleBeforeEachSourceCooksOnce()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(TimeSpan.FromMilliseconds(400));
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(TimeSpan.FromMilliseconds(400));
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("each change restarts the quiet window");
        fixture.Time.Advance(TimeSpan.FromMilliseconds(100));

        var cooked = new[] { await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false), await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false) };
        _ = cooked.Select(static call => call.AssetUri).Should().BeEquivalentTo([Main, Wood]);
    }

    /// <summary>Changes that keep coming do not hold the window open past its limit.</summary>
    /// <returns>The asynchronous window-limit regression.</returns>
    [TestMethod]
    public async Task WindowClosesAtItsLimitWhileChangesContinue()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();

        var elapsed = TimeSpan.Zero;
        while (elapsed < AutomaticCookService.QuietWindowLimit - TimeSpan.FromMilliseconds(400))
        {
            fixture.Change(AssetChangeKind.Updated, Main);
            fixture.Time.Advance(TimeSpan.FromMilliseconds(400));
            elapsed += TimeSpan.FromMilliseconds(400);
        }

        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindowLimit - elapsed);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Main);
    }

    /// <summary>A change while its source cooks cooks it once more afterwards, however many changes arrived.</summary>
    /// <returns>The asynchronous follow-up regression.</returns>
    [TestMethod]
    public async Task ChangesWhileCookingCookTheSourceOnceMore()
    {
        using var fixture = new Fixture();
        var release = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = release.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        for (var change = 0; change < 3; change++)
        {
            fixture.Change(AssetChangeKind.Updated, Main);
            fixture.Time.Advance(AutomaticCookService.QuietWindow);
        }

        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("the running cook is not interrupted or doubled");
        fixture.Result = Task.FromResult(Succeeded());
        release.SetResult(Succeeded());

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood, "the follow-up cooked its source only once");
    }

    /// <summary>A change resumes the run waiting for that source to be saved instead of adding a cook.</summary>
    /// <returns>The asynchronous resume regression.</returns>
    [TestMethod]
    public async Task ChangeResumesTheRunWaitingForThatSource()
    {
        using var fixture = new Fixture();
        var project = fixture.Projects.ActiveProject!;
        var operationId = Guid.NewGuid();
        var resumed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.InProgress =
        [
            new CookRunSnapshot
            {
                OperationId = operationId, ProjectId = project.ProjectId, ProjectRoot = project.ProjectRoot,
                DisplayName = "Project", Request = new(CookTargetKind.Project, ScopeUri: null), State = CookRunState.NeedsSave,
                UnsavedDocuments = [new(Guid.NewGuid(), fixture.SourcePath(Main), "Main", 1, 0, true, "old")],
            },
        ];
        _ = fixture.Runs.Setup(value => value.ResumeAfterSave(operationId)).Callback(resumed.SetResult).Returns(value: true);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        await resumed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood, "only the source no run waits for is cooked");
    }

    /// <summary>Cooked records, the project root's view of a source, output folders, files that do not cook and files a project opens with are not cooked.</summary>
    /// <returns>The asynchronous filter regression.</returns>
    [TestMethod]
    public async Task OnlyChangedCookableSourcesUnderAuthoringMountsCook()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, new Uri("asset:///Content/Scenes/Main.oscene"));
        fixture.Change(AssetChangeKind.Added, new Uri("asset:///project/Content/Scenes/Main.oscene.json"));
        fixture.Change(AssetChangeKind.Added, new Uri("asset:///Cooked/Scenes/Main.oscene.json"));
        fixture.Change(AssetChangeKind.Updated, new Uri("asset:///Content/Notes.txt"));
        fixture.Change(AssetChangeKind.Removed, Main);
        fixture.Change(AssetChangeKind.Listed, Main);
        fixture.Change(AssetChangeKind.Added, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A relocated source cancels the cook of its previous identity and cooks under its new one.</summary>
    /// <returns>The asynchronous relocation regression.</returns>
    [TestMethod]
    public async Task RelocationRetiresThePreviousSourceAndCooksTheNewOne()
    {
        using var fixture = new Fixture();
        fixture.Result = null;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        var previous = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        var renamed = new Uri("asset:///Content/Scenes/Demo.oscene.json");
        fixture.Change(AssetChangeKind.Relocated, renamed, Main);
        _ = previous.Token.IsCancellationRequested.Should().BeTrue();
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        var current = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);
        _ = current.AssetUri.Should().Be(renamed);
        _ = current.Token.IsCancellationRequested.Should().BeFalse();
    }

    /// <summary>Turning Cook on change off saves the setting, applies it at once, and leaves later changes uncooked.</summary>
    /// <returns>The asynchronous setting regression.</returns>
    [TestMethod]
    public async Task CookOnChangeOffIsSavedAndLeavesChangesUncooked()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();
        var notified = new List<string?>();
        service.PropertyChanged += (_, args) => notified.Add(args.PropertyName);

        await service.SetCookOnChangeAsync(enabled: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        fixture.Manager.Verify(value => value.SaveProjectSettingsAsync(fixture.Projects.ActiveProject!.ProjectRoot, new ProjectSettings { CookOnChange = false }, It.IsAny<CancellationToken>()), Times.Once);
        _ = service.CookOnChange.Should().BeFalse();
        _ = fixture.Projects.Settings.CookOnChange.Should().BeFalse();
        _ = notified.Should().Contain(nameof(IAutomaticCookService.CookOnChange));
        await service.SetCookOnChangeAsync(enabled: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood, "the change made while the setting was off stays uncooked");
    }

    /// <summary>A change that a cook in progress covers is not cooked again once that cook has published it.</summary>
    /// <returns>The asynchronous covering-cook regression.</returns>
    [TestMethod]
    public async Task ChangeCoveredByACookInProgressIsNotCookedAgain()
    {
        using var fixture = new Fixture();
        var project = fixture.Projects.ActiveProject!;
        var play = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(), ProjectId = project.ProjectId, ProjectRoot = project.ProjectRoot,
            DisplayName = "Main", Request = new(CookTargetKind.CurrentScene, Main), State = CookRunState.Cooking,
        };
        fixture.InProgress = [play];
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("the cook waits for the cook in progress");

        fixture.Fresh.Add(Main);
        fixture.InProgress = [];
        fixture.Runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(play with { State = CookRunState.Succeeded, CompletedAt = DateTimeOffset.UtcNow }));

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood, "only the change the finished cook did not cover is cooked");
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A change whose status cannot be read is cooked rather than left stale.</summary>
    /// <returns>The asynchronous fail-safe regression.</returns>
    [TestMethod]
    public async Task UnreadableStatusCooksTheChange()
    {
        using var fixture = new Fixture();
        fixture.StatusFailure = new IOException("Publication unavailable");
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Main);
    }

    /// <summary>A cook stopped for a requested cook is cooked again when that cook did not cover it.</summary>
    /// <returns>The asynchronous stopped-cook regression.</returns>
    [TestMethod]
    public async Task StoppedCookIsCookedAgainWhileStale()
    {
        using var fixture = new Fixture();
        var stopped = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = stopped.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        fixture.Result = Task.FromResult(Succeeded());
        stopped.SetException(new CookPreemptedException());

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Wood);
    }

    /// <summary>A cook stopped for a requested cook that covered it is not cooked again.</summary>
    /// <returns>The asynchronous covered stopped-cook regression.</returns>
    [TestMethod]
    public async Task StoppedCookCoveredByTheRequestedCookIsNotCookedAgain()
    {
        using var fixture = new Fixture();
        var project = fixture.Projects.ActiveProject!;
        var stopped = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = stopped.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        var requested = new CookRunSnapshot
        {
            OperationId = Guid.NewGuid(), ProjectId = project.ProjectId, ProjectRoot = project.ProjectRoot,
            DisplayName = "Materials", Request = new(CookTargetKind.Folder, new Uri("asset:///Content/Materials")), State = CookRunState.Cooking,
        };
        fixture.InProgress = [requested];
        fixture.Result = Task.FromResult(Succeeded());
        fixture.Fresh.Add(Wood);
        stopped.SetException(new CookPreemptedException());
        fixture.InProgress = [];
        fixture.Runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(requested with { State = CookRunState.Succeeded, CompletedAt = DateTimeOffset.UtcNow }));

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).AssetUri.Should().Be(Main, "the requested cook covered the stopped one");
    }

    private static ContentCookResult Succeeded() => new(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);

    private sealed class Fixture : IDisposable
    {
        private readonly Subject<AssetChange> changes = new();
        private readonly Mock<IContentPipelineService> pipeline = new();

        public Fixture()
        {
            this.Projects.Activate(new ProjectContext
            {
                ProjectId = Guid.NewGuid(),
                ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
                Name = "Cook on change",
                Category = Category.Games,
                AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
                LocalFolderMounts = [],
                Scenes = [],
            });
            _ = this.Runs.SetupGet(value => value.Runs).Returns(() => this.InProgress);
            _ = this.Statuses.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<Uri>>(), It.IsAny<CancellationToken>()))
                .Returns<ProjectContext, IReadOnlyList<Uri>, CancellationToken>((_, assets, _) => this.StatusFailure is { } failure
                    ? Task.FromException<IReadOnlyList<AssetCookStatus>>(failure)
                    : Task.FromResult<IReadOnlyList<AssetCookStatus>>([.. assets.Select(asset => new AssetCookStatus(
                        asset,
                        this.Fresh.Contains(asset) ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate,
                        HasPublishedOutput: true,
                        CookedOutputAvailability.Present,
                        [],
                        [],
                        ImmutableArray<DiagnosticRecord>.Empty))]));
            _ = this.pipeline.Setup(value => value.CookSavedAssetAsync(It.IsAny<Uri>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
                .Returns<Uri, ProjectContext, CancellationToken>(async (uri, project, token) =>
                {
                    _ = this.Calls.Writer.TryWrite((uri, token));
                    if (this.Result is { } result)
                    {
                        return await result.ConfigureAwait(false);
                    }

                    await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
                    return Succeeded();
                });
            _ = this.Manager.Setup(value => value.SaveProjectSettingsAsync(It.IsAny<string>(), It.IsAny<ProjectSettings>(), It.IsAny<CancellationToken>()))
                .Returns(Task.CompletedTask);
        }

        public ProjectContextService Projects { get; } = new();

        public Mock<ICookRunService> Runs { get; } = new();

        public Mock<IProjectManagerService> Manager { get; } = new();

        public Mock<IAssetCookStatusReader> Statuses { get; } = new();

        public HashSet<Uri> Fresh { get; } = [];

        public Exception? StatusFailure { get; set; }

        public IReadOnlyList<CookRunSnapshot> InProgress { get; set; } = [];

        public ManualTime Time { get; } = new();

        public Channel<(Uri AssetUri, CancellationToken Token)> Calls { get; } = Channel.CreateUnbounded<(Uri AssetUri, CancellationToken Token)>();

        public Task<ContentCookResult>? Result { get; set; } = Task.FromResult(Succeeded());

        public AutomaticCookService CreateService()
            => new(this.Projects, this.Manager.Object, Mock.Of<IAssetCatalog>(catalog => catalog.Changes == this.changes), this.pipeline.Object, this.Statuses.Object, this.Runs.Object,
                NullLogger<AutomaticCookService>.Instance, this.Time);

        public void Change(AssetChangeKind kind, Uri uri, Uri? previous = null) => this.changes.OnNext(new(kind, uri, previous));

        public string SourcePath(Uri uri)
            => Path.GetFullPath(Path.Combine(this.Projects.ActiveProject!.ProjectRoot, Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/')));

        public async Task<(Uri AssetUri, CancellationToken Token)> NextCallAsync(TestContext context)
            => await this.Calls.Reader.ReadAsync(context.CancellationToken).AsTask().WaitAsync(TimeSpan.FromSeconds(5), context.CancellationToken).ConfigureAwait(false);

        public void Dispose() => this.changes.Dispose();
    }

    /// <summary>A clock the test advances; timers fire on the advancing thread.</summary>
    private sealed class ManualTime : TimeProvider
    {
        private readonly List<ManualTimer> timers = [];
        private long now;

        public override long TimestampFrequency => TimeSpan.TicksPerSecond;

        public override long GetTimestamp() => this.now;

        public override ITimer CreateTimer(TimerCallback callback, object? state, TimeSpan dueTime, TimeSpan period)
        {
            var timer = new ManualTimer(this, callback, state);
            this.timers.Add(timer);
            _ = timer.Change(dueTime, period);
            return timer;
        }

        public void Advance(TimeSpan by)
        {
            this.now += by.Ticks;
            foreach (var timer in this.timers.ToArray())
            {
                timer.FireIfDue();
            }
        }

        private sealed class ManualTimer(ManualTime owner, TimerCallback callback, object? state) : ITimer
        {
            private long? due;

            public bool Change(TimeSpan dueTime, TimeSpan period)
            {
                this.due = dueTime == Timeout.InfiniteTimeSpan ? null : owner.now + dueTime.Ticks;
                return true;
            }

            public void FireIfDue()
            {
                if (this.due is { } at && at <= owner.now)
                {
                    this.due = null;
                    callback(state);
                }
            }

            public void Dispose() => this.due = null;

            public ValueTask DisposeAsync()
            {
                this.Dispose();
                return ValueTask.CompletedTask;
            }
        }
    }
}
