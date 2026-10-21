// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reactive.Subjects;
using System.Threading.Channels;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Input;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Scripting.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AutomaticCookServiceTests
{
    private static readonly AssetPath Main = AssetPath.Parse("/Content/Scenes/Main.oscene.json");
    private static readonly AssetPath Wood = AssetPath.Parse("/Content/Materials/Wood.omat.json");
    private static readonly AssetPath Orbit = AssetPath.Parse("/Content/Scripts/Orbit.lua");
    private static readonly AssetPath Player = AssetPath.Parse("/Content/Input/Player.input.json");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Changes wait out the quiet window, which each change restarts, then one run covers every changed source.</summary>
    /// <returns>The asynchronous quiet-window regression.</returns>
    [TestMethod]
    public async Task ChangesSettleIntoOneRun()
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

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, Wood]);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
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

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
    }

    /// <summary>Changes that arrive while a run cooks collect into one next run, which starts when it ends.</summary>
    /// <returns>The asynchronous follow-up regression.</returns>
    [TestMethod]
    public async Task ChangesWhileARunCooksCollectIntoTheNextRun()
    {
        using var fixture = new Fixture();
        var release = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = release.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        var running = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        for (var change = 0; change < 3; change++)
        {
            fixture.Change(AssetChangeKind.Updated, Main);
            fixture.Time.Advance(AutomaticCookService.QuietWindow);
            fixture.Change(AssetChangeKind.Updated, Wood);
            fixture.Time.Advance(AutomaticCookService.QuietWindow);
        }

        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("the running run is not interrupted or doubled");
        _ = running.Token.IsCancellationRequested.Should().BeFalse();
        fixture.Result = Task.FromResult(Succeeded());
        release.SetResult(Succeeded());

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, Wood]);
        fixture.Change(AssetChangeKind.Updated, Player);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal([Player], "the changes during the run cooked once");
    }

    /// <summary>Changes that close while the run waits for the writer join it; no second run queues behind it.</summary>
    /// <returns>The asynchronous waiting-run regression.</returns>
    [TestMethod]
    public async Task ChangesWhileTheRunWaitsForTheWriterJoinIt()
    {
        using var fixture = new Fixture();
        var busy = fixture.HoldWriter();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("the run waits for the work in progress");
        busy.SetResult();

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, Wood]);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>Cooked records, the project root's view of a source, output folders, files that do not cook and files a project opens with are not cooked.</summary>
    /// <returns>The asynchronous filter regression.</returns>
    [TestMethod]
    public async Task OnlyChangedCookableSourcesUnderAuthoringMountsCook()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, AssetPath.Parse("/Content/Scenes/Main.oscene"));
        fixture.Change(AssetChangeKind.Added, AssetPath.Parse("/project/Content/Scenes/Main.oscene.json"));
        fixture.Change(AssetChangeKind.Added, AssetPath.Parse("/Cooked/Scenes/Main.oscene.json"));
        fixture.Change(AssetChangeKind.Updated, AssetPath.Parse("/Content/Notes.txt"));
        fixture.Change(AssetChangeKind.Listed, Main);
        fixture.Change(AssetChangeKind.Added, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Wood);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A saved script cooks on change, like an authored descriptor.</summary>
    /// <returns>The asynchronous script regression.</returns>
    [TestMethod]
    public async Task ChangedScriptCooks()
    {
        using var fixture = new Fixture();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Orbit);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Orbit);
    }

    /// <summary>A relocation during a run does not stop it; both of the source's identities go into the next run.</summary>
    /// <returns>The asynchronous relocation regression.</returns>
    [TestMethod]
    public async Task RelocationDuringARunGoesIntoTheNextRun()
    {
        using var fixture = new Fixture();
        var release = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = release.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        var running = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        var renamed = AssetPath.Parse("/Content/Scenes/Demo.oscene.json");
        fixture.Gone.Add(Main);
        fixture.Change(AssetChangeKind.Relocated, renamed, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = running.Token.IsCancellationRequested.Should().BeFalse();
        fixture.Result = Task.FromResult(Succeeded());
        release.SetResult(Succeeded());

        // The previous identity is cooked too, to remove its output.
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, renamed]);
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

        fixture.Manager.Verify(value => value.SaveProjectSettingsAsync(fixture.Projects.ActiveProject!.ProjectRoot.FullPath, new ProjectSettings { CookOnChange = false }, It.IsAny<CancellationToken>()), Times.Once);
        _ = service.CookOnChange.Should().BeFalse();
        _ = fixture.Projects.Settings.CookOnChange.Should().BeFalse();
        _ = notified.Should().Contain(nameof(IAutomaticCookService.CookOnChange));
        await service.SetCookOnChangeAsync(enabled: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal([Wood], "the change made while the setting was off stays uncooked");
    }

    /// <summary>Changes wait until the project writer is idle; one that the finished work covered is not cooked again.</summary>
    /// <returns>The asynchronous covering-cook regression.</returns>
    [TestMethod]
    public async Task ChangeCoveredByWorkInProgressIsNotCookedAgain()
    {
        using var fixture = new Fixture();
        var busy = fixture.HoldWriter();
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse("the run waits for the work in progress");

        fixture.Fresh.Add(Main);
        busy.SetResult();

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal([Wood], "only the change the finished cook did not cover is cooked");
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A changed source whose file is gone when the run starts, and that was never cooked, is not cooked.</summary>
    /// <returns>The asynchronous gone-source regression.</returns>
    [TestMethod]
    public async Task GoneSourceWithoutOutputIsNotCooked()
    {
        using var fixture = new Fixture();
        fixture.Gone.Add(Wood);
        _ = fixture.NeverCooked.Add(Wood);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A changed source that cannot be a cook input, such as a deleted script, is not cooked.</summary>
    /// <returns>The asynchronous unresolved-source regression.</returns>
    [TestMethod]
    public async Task SourceThatCannotBeACookInputIsNotCooked()
    {
        using var fixture = new Fixture();
        fixture.Unresolved.Add(Orbit);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Removed, Orbit);
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
    }

    /// <summary>A deleted source that has cooked output is cooked, which removes that output.</summary>
    /// <returns>The asynchronous removal regression.</returns>
    [TestMethod]
    public async Task DeletedSourceWithOutputIsCookedToRemoveIt()
    {
        using var fixture = new Fixture();
        fixture.Gone.Add(Main);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Removed, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
    }

    /// <summary>A renamed source's previous identity is cooked to remove its output, in the run that cooks the new one.</summary>
    /// <returns>The asynchronous rename regression.</returns>
    [TestMethod]
    public async Task RenamedSourceRemovesThePreviousOutputAndCooksTheNewOne()
    {
        using var fixture = new Fixture();
        fixture.Gone.Add(Main);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Relocated, Wood, previous: Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, Wood]);
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

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
    }

    /// <summary>A run stopped for a requested cook is cooked again when that cook did not cover it.</summary>
    /// <returns>The asynchronous stopped-run regression.</returns>
    [TestMethod]
    public async Task StoppedRunIsCookedAgainWhileStale()
    {
        using var fixture = new Fixture();
        var stopped = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = stopped.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        fixture.Result = Task.FromResult(Succeeded());
        stopped.SetException(new CookPreemptedException());

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().BeEquivalentTo([Main, Wood]);
    }

    /// <summary>A run stopped for a requested cook cooks again only what that cook did not cover.</summary>
    /// <returns>The asynchronous covered stopped-run regression.</returns>
    [TestMethod]
    public async Task StoppedRunCooksOnlyWhatTheRequestedCookDidNotCover()
    {
        using var fixture = new Fixture();
        var stopped = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        fixture.Result = stopped.Task;
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);

        var requested = fixture.HoldWriter();
        fixture.Result = Task.FromResult(Succeeded());
        stopped.SetException(new CookPreemptedException());
        fixture.Fresh.Add(Wood);
        requested.SetResult();

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal([Main], "the requested cook covered the other source");
    }

    /// <summary>A source whose facts report errors is left out of the run, which names it; the others cook.</summary>
    /// <returns>The asynchronous skip regression.</returns>
    [TestMethod]
    public async Task SourcesWithErrorsAreLeftOutAndNamed()
    {
        using var fixture = new Fixture();
        _ = fixture.Scripts.Add(OrbitScript(errors: 2));
        fixture.Inputs.SetProblems([InputError()]);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Orbit);
        fixture.Change(AssetChangeKind.Updated, Player);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        var call = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);
        _ = call.Sources.Should().Equal(Wood);
        _ = call.Changes.Skipped.Should().BeEquivalentTo(
        [
            new SkippedCookSource { Source = Orbit, ErrorCount = 2 },
            new SkippedCookSource { Source = Player, ErrorCount = 1 },
        ]);
    }

    /// <summary>Warnings and problems of other sources do not leave a source out.</summary>
    /// <returns>The asynchronous severity regression.</returns>
    [TestMethod]
    public async Task OnlyItsOwnErrorsLeaveASourceOut()
    {
        using var fixture = new Fixture();
        _ = fixture.Scripts.Add(OrbitScript(errors: 0, warnings: 1));
        fixture.Inputs.SetProblems([InputError(AssetPath.Parse("/Content/Input/Other.input"))]);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Orbit);
        fixture.Change(AssetChangeKind.Updated, Player);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        var call = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);
        _ = call.Sources.Should().BeEquivalentTo([Orbit, Player]);
        _ = call.Changes.Skipped.Should().BeEmpty();
    }

    /// <summary>When every changed source has errors, no run starts.</summary>
    /// <returns>The asynchronous empty-run regression.</returns>
    [TestMethod]
    public async Task NoRunWhenEveryChangedSourceHasErrors()
    {
        using var fixture = new Fixture();
        _ = fixture.Scripts.Add(OrbitScript(errors: 1));
        using var service = fixture.CreateService();

        await fixture.SkipAsync(this.TestContext).ConfigureAwait(false);
    }

    /// <summary>The change that fixes a skipped source's errors cooks it, in one run.</summary>
    /// <returns>The asynchronous fixing-change regression.</returns>
    [TestMethod]
    public async Task ChangeThatFixesTheErrorsCooksTheSource()
    {
        using var fixture = new Fixture();
        _ = fixture.Scripts.Add(OrbitScript(errors: 1));
        using var service = fixture.CreateService();
        await fixture.SkipAsync(this.TestContext).ConfigureAwait(false);

        _ = fixture.Scripts.Add(OrbitScript(errors: 0));
        fixture.Change(AssetChangeKind.Updated, Orbit);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Orbit);
        _ = fixture.Calls.Reader.TryRead(out _).Should().BeFalse();
    }

    /// <summary>A skipped source cooks once its facts report no errors, when they catch up after its fixing change.</summary>
    /// <returns>The asynchronous late-facts regression.</returns>
    [TestMethod]
    public async Task SkippedSourceCooksWhenItsFactsCatchUp()
    {
        using var fixture = new Fixture();
        _ = fixture.Scripts.Add(OrbitScript(errors: 1));
        using var service = fixture.CreateService();
        await fixture.SkipAsync(this.TestContext).ConfigureAwait(false);

        _ = fixture.Scripts.Add(OrbitScript(errors: 0));
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Orbit);
    }

    /// <summary>Facts still behind a breaking change are brought up to date before the run, which leaves the source out.</summary>
    /// <returns>The asynchronous late-facts regression.</returns>
    [TestMethod]
    public async Task FactsBehindABreakingChangeAreCheckedBeforeTheRun()
    {
        using var fixture = new Fixture();
        fixture.Scripts.SetPending(OrbitScript(errors: 1));
        fixture.Inputs.SetPendingProblems([InputError()]);
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Orbit);
        fixture.Change(AssetChangeKind.Updated, Player);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        var call = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);
        _ = call.Sources.Should().Equal(Wood);
        _ = call.Changes.Skipped.Select(static skipped => skipped.Source).Should().BeEquivalentTo([Orbit, Player]);
    }

    /// <summary>A source that uses a source with errors, such as a scene whose script has errors, is left out and names it.</summary>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    public async Task SourceThatUsesASourceWithErrorsIsLeftOut()
    {
        using var fixture = new Fixture();
        fixture.Uses[Main] = [Orbit];
        _ = fixture.Scripts.Add(OrbitScript(errors: 2));
        using var service = fixture.CreateService();

        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        var call = await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false);
        _ = call.Sources.Should().Equal(Wood);
        var skipped = call.Changes.Skipped.Should().ContainSingle().Which;
        _ = (skipped.Source, skipped.ErrorCount).Should().Be((Main, 2));
        _ = skipped.Dependencies.Should().Equal(Orbit);
        _ = fixture.Scripts.Checks.Should().ContainSingle().Which.Should().Contain(Orbit, "the facts of what the run reads are checked");
    }

    /// <summary>A source left out for a dependency's errors cooks once that dependency's errors are fixed.</summary>
    /// <returns>The asynchronous dependency-fix regression.</returns>
    [TestMethod]
    public async Task SourceLeftOutForADependencyCooksWhenItIsFixed()
    {
        using var fixture = new Fixture();
        fixture.Uses[Main] = [Orbit];
        _ = fixture.Scripts.Add(OrbitScript(errors: 1));
        using var service = fixture.CreateService();
        fixture.Change(AssetChangeKind.Updated, Main);
        fixture.Change(AssetChangeKind.Updated, Wood);
        fixture.Time.Advance(AutomaticCookService.QuietWindow);
        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Wood);

        _ = fixture.Scripts.Add(OrbitScript(errors: 0));
        fixture.Time.Advance(AutomaticCookService.QuietWindow);

        _ = (await fixture.NextCallAsync(this.TestContext).ConfigureAwait(false)).Sources.Should().Equal(Main);
    }

    private static ContentCookResult Succeeded() => new(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null);

    private static Script OrbitScript(int errors, int warnings = 0) => new()
    {
        Identity = AssetPath.Parse("/Content/Scripts/Orbit.oscript"),
        Origin = ScriptOrigin.Source,
        Problems =
        [
            .. Enumerable.Range(0, errors).Select(static _ => new ScriptProblem(DiagnosticSeverity.Error, "script.syntax", "Unexpected symbol.", "Orbit.lua:1: unexpected symbol")),
            .. Enumerable.Range(0, warnings).Select(static _ => new ScriptProblem(DiagnosticSeverity.Warning, "script.unused", "Unused local.", "Orbit.lua:2: unused local")),
        ],
    };

    private static InputProblem InputError(AssetPath? container = null)
        => new(new AssetProblem(DiagnosticSeverity.Error, "input.action_path", "The action is not declared."), container ?? AssetPath.Parse("/Content/Input/Player.input"));

    private sealed class Fixture : IDisposable
    {
        private readonly Subject<AssetChange> changes = new();
        private readonly Mock<IContentPipelineService> pipeline = new();
        private TaskCompletionSource writer = IdleWriter();

        public Fixture()
        {
            this.Projects.Activate(TestProject.Create(new MockFileSystem()).Context with
            {
                Name = "Cook on change",
                AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
            });
            _ = this.Runs.SetupGet(value => value.Runs).Returns(() => this.InProgress);
            _ = this.Runs.Setup(value => value.WhenWriterIdleAsync(It.IsAny<CancellationToken>()))
                .Returns<CancellationToken>(token => this.writer.Task.WaitAsync(token));
            _ = this.Statuses.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<IReadOnlyList<AssetPath>>(), It.IsAny<CancellationToken>()))
                .Returns<ProjectContext, IReadOnlyList<AssetPath>, CancellationToken>((_, assets, _) =>
                {
                    _ = this.StatusReads.Writer.TryWrite([.. assets]);
                    return this.StatusFailure is { } failure
                        ? Task.FromException<IReadOnlyList<AssetCookStatus>>(failure)
                        : Task.FromResult<IReadOnlyList<AssetCookStatus>>([.. assets.Select(this.StatusOf)]);
                });
            _ = this.pipeline.Setup(value => value.CookChangedSourcesAsync(It.IsAny<CookChanges>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
                .Returns<CookChanges, ProjectContext, CancellationToken>(async (changes, project, token) =>
                {
                    _ = this.Calls.Writer.TryWrite(new(changes, token));
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

        public FakeScripts Scripts { get; } = new();

        public FakeInputSources Inputs { get; } = new();

        public HashSet<AssetPath> Fresh { get; } = [];

        public HashSet<AssetPath> Gone { get; } = [];

        public HashSet<AssetPath> NeverCooked { get; } = [];

        public HashSet<AssetPath> Unresolved { get; } = [];

        public Dictionary<AssetPath, AssetPath[]> Uses { get; } = [];

        public Exception? StatusFailure { get; set; }

        public IReadOnlyList<CookRunSnapshot> InProgress { get; set; } = [];

        public ManualTime Time { get; } = new();

        public Channel<CookCall> Calls { get; } = Channel.CreateUnbounded<CookCall>();

        public Channel<AssetPath[]> StatusReads { get; } = Channel.CreateUnbounded<AssetPath[]>();

        public Task<ContentCookResult>? Result { get; set; } = Task.FromResult(Succeeded());

        /// <summary>Holds the project writer until the returned source completes.</summary>
        /// <returns>The source that releases the writer.</returns>
        public TaskCompletionSource HoldWriter()
        {
            this.writer = new(TaskCreationOptions.RunContinuationsAsynchronously);
            return this.writer;
        }

        public AutomaticCookService CreateService()
            => new(
                this.Projects,
                this.Manager.Object,
                Mock.Of<IAssetCatalog>(catalog => catalog.Changes == this.changes),
                this.pipeline.Object,
                this.Statuses.Object,
                this.Runs.Object,
                this.Scripts,
                this.Inputs,
                NullLogger<AutomaticCookService>.Instance,
                this.Time);

        public void Change(AssetChangeKind kind, AssetPath uri, AssetPath? previous = null) => this.changes.OnNext(new(kind, uri, previous));

        public FileSystemPath SourcePath(AssetPath asset)
            => this.Projects.ActiveProject!.ProjectRoot.Combine(asset.VirtualPath.TrimStart('/'));

        public async Task<CookCall> NextCallAsync(TestContext context)
            => await this.Calls.Reader.ReadAsync(context.CancellationToken).AsTask().WaitAsync(TimeSpan.FromSeconds(5), context.CancellationToken).ConfigureAwait(false);

        /// <summary>Changes the script, whose facts report errors, and checks that the run of a later change leaves it out.</summary>
        /// <param name="context">The test context.</param>
        /// <returns>The asynchronous skip.</returns>
        public async Task SkipAsync(TestContext context)
        {
            this.Change(AssetChangeKind.Updated, Orbit);
            this.Time.Advance(AutomaticCookService.QuietWindow);
            _ = (await this.StatusReads.Reader.ReadAsync(context.CancellationToken).AsTask().WaitAsync(TimeSpan.FromSeconds(5), context.CancellationToken).ConfigureAwait(false))
                .Should().Equal(Orbit);

            // Runs are taken one after another, so the next one starts after the script was left out.
            this.Change(AssetChangeKind.Updated, Wood);
            this.Time.Advance(AutomaticCookService.QuietWindow);
            var call = await this.NextCallAsync(context).ConfigureAwait(false);
            _ = call.Sources.Should().Equal([Wood], "no run started for the script alone");
            _ = call.Changes.Skipped.Should().BeEmpty();
        }

        public void Dispose() => this.changes.Dispose();

        private static TaskCompletionSource IdleWriter()
        {
            var idle = new TaskCompletionSource();
            idle.SetResult();
            return idle;
        }

        private AssetCookStatus StatusOf(AssetPath asset)
            => new(
                asset,
                this.Unresolved.Contains(asset) ? AssetCookFreshness.InvalidSource
                    : this.Fresh.Contains(asset) ? AssetCookFreshness.Current
                    : this.Gone.Contains(asset) ? AssetCookFreshness.MissingSource
                    : AssetCookFreshness.OutOfDate,
                HasPublishedOutput: !this.NeverCooked.Contains(asset) && !this.Unresolved.Contains(asset),
                CookedOutputAvailability.Present,
                [],
                this.Unresolved.Contains(asset)
                    ? [new DiagnosticRecord { OperationId = Guid.Empty, Domain = FailureDomain.AssetCook, Severity = DiagnosticSeverity.Error, Code = AssetCookStatusReader.InputUnresolvedCode, Message = "Unsupported cook dependency." }]
                    : ImmutableArray<DiagnosticRecord>.Empty)
            {
                SourcePaths = [.. this.Uses.GetValueOrDefault(asset, []).Prepend(asset).Select(this.SourcePath)],
            };
    }

    /// <summary>One run the service asked the pipeline for.</summary>
    /// <param name="Changes">The changed sources it cooks and those it leaves out.</param>
    /// <param name="Token">The run's cancellation.</param>
    private sealed record CookCall(CookChanges Changes, CancellationToken Token)
    {
        public IReadOnlyList<AssetPath> Sources => this.Changes.Sources;
    }

    /// <summary>A clock the test advances; timers fire on the advancing thread.</summary>
    private sealed class ManualTime : System.TimeProvider
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
