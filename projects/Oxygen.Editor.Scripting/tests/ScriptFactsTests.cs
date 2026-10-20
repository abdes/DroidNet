// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Disposables;
using System.Reactive.Subjects;
using System.Threading.Channels;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting.Testing;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scripting.Tests;

/// <summary>
/// The script facts hold the open project's scripts as their analyzer reads them, follow each change of a source, keep
/// a broken declaration's last valid parameters, and read library scripts through the library port.
/// </summary>
[TestClass]
public sealed partial class ScriptFactsTests : IDisposable
{
    private const string MoveFile = "/Content/Player/move.lua";
    private const string MoveSource = "return oxygen.script { params = { speed = oxygen.param.float { default = 1 } } }";
    private const string BrokenSource = "return oxygen.script { params = { speed = } }";
    private const string OlderSource = "return oxygen.script { params = { spin = 1 } }";
    private const string NewerSource = "return oxygen.script { params = { turn = 2 } }";
    private const string RunFile = "/Content/Player/run.lua";

    private static readonly AssetPath Move = AssetPath.Parse("/Content/Player/move.oscript");
    private static readonly TimeSpan Patience = TimeSpan.FromSeconds(10);
    private static readonly AssetPath Run = AssetPath.Parse("/Content/Player/run.oscript");
    private static readonly ScriptParameter Spin = new() { Name = "spin", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(1f) };
    private static readonly ScriptParameter Turn = new() { Name = "turn", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(2f) };
    private static readonly ScriptParameter Speed = new() { Name = "speed", Kind = ScriptParameterKind.FloatParameter, Default = ScriptValue.FromFloat(1f) };

    private readonly TestProject project = TestProject.Create(new MockFileSystem());
    private readonly ProjectContextService projects = new();
    private readonly Subject<AssetChange> changes = new();
    private readonly FakeScriptAnalyzer analyzer = new();
    private readonly FakeScriptLibrary library = new();
    private readonly List<OperationResult> published = [];
    private readonly RecordingResults results;
    private readonly ScriptFacts facts;

    public ScriptFactsTests()
    {
        this.analyzer.Set(MoveSource, new ScriptAnalysis { HasDeclaration = true, Parameters = [Speed] });
        this.analyzer.Set(OlderSource, new ScriptAnalysis { HasDeclaration = true, Parameters = [Spin] });
        this.analyzer.Set(NewerSource, new ScriptAnalysis { HasDeclaration = true, Parameters = [Turn] });
        this.analyzer.Set(BrokenSource, new ScriptAnalysis
        {
            HasDeclaration = true,
            Problems = [new ScriptProblem(DiagnosticSeverity.Error, "S0101", "unexpected symbol", "rendered") { Line = 1, Column = 43 }],
        });
        this.results = new RecordingResults(this.published);
        this.facts = new ScriptFacts(
            this.projects,
            new Catalog(this.changes),
            this.project.Context.FileSystem,
            this.analyzer,
            this.library,
            this.results,
            new ScriptToolingPaths(@"C:\Sdk\oxygen.d.luau", @"C:\Sdk\schemas"));
    }

    public TestContext TestContext { get; set; }

    public void Dispose()
    {
        this.facts.Dispose();
        this.changes.Dispose();
    }

    [TestMethod]
    public async Task TheScriptsOfAProjectAreAnalyzedWhenItOpens()
    {
        var file = this.project.WriteFile(MoveFile, MoveSource);

        await this.OpenAsync().ConfigureAwait(false);

        var script = this.facts.Find(Move)!;
        _ = (script.Origin, script.SourcePath).Should().Be((ScriptOrigin.Source, file));
        _ = script.Parameters.Should().Equal(Speed);
        _ = this.facts.All.Should().ContainSingle();
        _ = this.analyzer.Analyzed.Should().Equal("Content/Player/move.lua");
    }

    [TestMethod]
    public async Task AProblemNamesItsFileAndWhereItIs()
    {
        var file = this.project.WriteFile(MoveFile, BrokenSource);

        await this.OpenAsync().ConfigureAwait(false);

        var problem = this.facts.Problems.Should().ContainSingle().Which.Should().BeOfType<ScriptProblem>().Subject;
        _ = problem.Code.Should().Be("S0101");
        _ = (problem.File, problem.Line).Should().Be((file.FullPath, 1));
    }

    [TestMethod]
    public async Task ABrokenDeclarationKeepsTheLastValidParametersOutOfDate()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);

        _ = this.project.WriteFile(MoveFile, BrokenSource);
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Updated, AssetPath.Parse(MoveFile)), () => this.facts.Problems.Count == 1).ConfigureAwait(false);

        var script = this.facts.Find(Move)!;
        _ = script.ParametersOutOfDate.Should().BeTrue();
        _ = script.Parameters.Should().Equal(Speed);
    }

    [TestMethod]
    public async Task AnUnchangedSourceIsAnalyzedAgainOnlyWhenForced()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);

        await this.facts.RefreshAsync(force: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = this.analyzer.Analyzed.Should().ContainSingle();

        await this.facts.RefreshAsync(force: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = this.analyzer.Analyzed.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task ARemovedSourceIsForgotten()
    {
        var file = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);

        this.project.Context.FileSystem.File.Delete(file.FullPath);
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Removed, AssetPath.Parse(MoveFile)), () => this.facts.All.Count == 0).ConfigureAwait(false);

        _ = this.facts.Find(Move).Should().BeNull();
    }

    [TestMethod]
    public async Task AMovedSourceIsFoundAtItsNewIdentity()
    {
        var file = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);

        this.project.Context.FileSystem.File.Move(file.FullPath, this.project.PathOf("/Content/Player/run.lua").FullPath);
        await this.ChangeAsync(
            new AssetChange(AssetChangeKind.Relocated, AssetPath.Parse("/Content/Player/run.lua"), AssetPath.Parse(MoveFile)),
            () => this.facts.Find(Move) is null && this.facts.All.Count == 1).ConfigureAwait(false);

        _ = this.facts.All.Should().ContainSingle().Which.Identity.Should().Be(AssetPath.Parse("/Content/Player/run.oscript"));
    }

    [TestMethod]
    public async Task AnalysisWaitsUntilTheAnalyzerIsAvailable()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        this.analyzer.MakeUnavailable();
        this.projects.Activate(this.project.Context);
        _ = this.facts.All.Should().BeEmpty();

        await this.UntilAsync(() => this.facts.All.Count == 1, this.analyzer.MakeAvailable).ConfigureAwait(false);

        _ = this.facts.Find(Move).Should().NotBeNull();
    }

    [TestMethod]
    public async Task AnAnalyzerThatCannotRunIsReportedOnce()
    {
        _ = this.project.WriteFile(MoveFile, "first");
        _ = this.project.WriteFile("/Content/Player/run.lua", "second");
        this.analyzer.Set("first", ScriptAnalysis.Unavailable("missing native library"));
        this.analyzer.Set("second", ScriptAnalysis.Unavailable("missing native library"));

        // The scan of the opened project reports on its own thread; a forced analysis afterwards reports nothing more.
        this.projects.Activate(this.project.Context);
        await this.results.FirstPublished.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);
        await this.facts.RefreshAsync(force: true, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.facts.All.Should().BeEmpty();
        _ = this.published.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle().Which.Code.Should().Be(ScriptDiagnosticCodes.AnalysisUnavailable);
    }

    [TestMethod]
    public async Task ClosingTheProjectForgetsItsScripts()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);

        this.projects.Close();

        _ = this.facts.All.Should().BeEmpty();
        _ = this.facts.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ALibraryScriptIsReadThroughTheLibraryAndAFailedReadSaysWhy()
    {
        var rotate = new Script { Identity = AssetPath.Parse("/Engine/Scripts/rotate.oscript"), Origin = ScriptOrigin.Library, Parameters = [Speed] };
        var broken = AssetPath.Parse("/Engine/Scripts/broken.oscript");
        this.library.Add(rotate);
        this.library.Fail(broken, new InvalidDataException("the report does not match its schema"));
        this.projects.Activate(this.project.Context);

        var found = await this.facts.ReadLibraryAsync(rotate.Identity, this.TestContext.CancellationToken).ConfigureAwait(false);
        var failed = await this.facts.ReadLibraryAsync(broken, this.TestContext.CancellationToken).ConfigureAwait(false);
        var absent = await this.facts.ReadLibraryAsync(AssetPath.Parse("/Engine/Scripts/none.oscript"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = found.Script.Should().BeSameAs(rotate);
        _ = (failed.Script, failed.Failure).Should().Be((null, "the report does not match its schema"));
        _ = absent.Should().BeSameAs(ScriptRead.NotFound);
    }

    [TestMethod]
    public async Task EventsAreRaisedOnTheSubscribersContext()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        var context = new RecordingContext();
        var raised = new TaskCompletionSource<SynchronizationContext?>(TaskCreationOptions.RunContinuationsAsynchronously);
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(context);
        try
        {
            this.facts.Changed += (_, args) =>
            {
                if (args.Script == Move)
                {
                    _ = raised.TrySetResult(SynchronizationContext.Current);
                }
            };
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }

        this.projects.Activate(this.project.Context);

        _ = (await raised.Task.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeSameAs(context);
    }

    [TestMethod]
    public async Task SwitchingProjectsReplacesTheScriptsWithTheNewProjectsOwn()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);
        var other = TestProject.Create(this.project.Context.FileSystem, @"C:\Other");
        var file = other.WriteFile(RunFile, MoveSource);

        await this.UntilAsync(() => this.facts.Find(Run) is not null, () => this.projects.Activate(other.Context)).ConfigureAwait(false);

        _ = this.facts.Find(Move).Should().BeNull();
        _ = this.facts.All.Should().ContainSingle().Which.SourcePath.Should().Be(file);
        _ = this.analyzer.Analyzed.Should().Equal("Content/Player/move.lua", "Content/Player/run.lua");
    }

    [TestMethod]
    public async Task ABrokenDeclarationWithNoEarlierValidVersionHasNoParameters()
    {
        _ = this.project.WriteFile(MoveFile, BrokenSource);

        await this.OpenAsync().ConfigureAwait(false);

        var script = this.facts.Find(Move)!;
        _ = script.Parameters.Should().BeEmpty();
        _ = script.ParametersOutOfDate.Should().BeFalse();
    }

    [TestMethod]
    public async Task WhenTwoAnalysesOfOneFileOverlapTheNewerIsPublishedAndTheOlderPublishesNothing()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);
        var older = this.analyzer.HoldNext();

        // The older analysis has read its file, and waits, before the file changes again. A refresh runs it, so the
        // test knows when it has finished.
        _ = this.project.WriteFile(MoveFile, OlderSource);
        var olderRun = this.facts.RefreshAsync(force: true, this.TestContext.CancellationToken);
        await older.Arrived.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = this.project.WriteFile(MoveFile, NewerSource);
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Updated, AssetPath.Parse(MoveFile)), () => this.facts.Find(Move)!.Parameters.Contains(Turn)).ConfigureAwait(false);

        older.Release();
        await olderRun.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.facts.Find(Move)!.Parameters.Should().Equal(Turn);
    }

    [TestMethod]
    public async Task AFileThatCannotBeReadKeepsItsPreviousScript()
    {
        var file = this.project.WriteFile(MoveFile, MoveSource);
        await this.OpenAsync().ConfigureAwait(false);
        _ = this.project.WriteFile(RunFile, NewerSource);
        await using var lockedFile = this.project.Context.FileSystem.File.Open(file.FullPath, FileMode.Open, FileAccess.Read, FileShare.None).ConfigureAwait(false);

        // One change names both files, and its analyses run in order: the unreadable one, then the readable one.
        await this.ChangeAsync(
            new AssetChange(AssetChangeKind.Relocated, AssetPath.Parse(RunFile), AssetPath.Parse(MoveFile)),
            () => this.facts.Find(Run) is not null).ConfigureAwait(false);

        _ = this.facts.Find(Move)!.Parameters.Should().Equal(Speed);
        _ = this.analyzer.Analyzed.Should().Equal("Content/Player/move.lua", "Content/Player/run.lua");
    }

    [TestMethod]
    public async Task TheScanSkipsDotFoldersOutputFoldersAndFilesThatAreNotScripts()
    {
        var other = TestProject.Create(this.project.Context.FileSystem, @"C:\Outputs", [TestProject.ContentMount, ".cooked"]);
        _ = other.WriteFile(MoveFile, MoveSource);
        _ = other.WriteFile("/Content/readme.txt", MoveSource);
        WriteRaw(other, other.Root.Combine(TestProject.ContentMount).Combine(".hidden").Combine("a.lua"));
        WriteRaw(other, other.Root.Combine(".cooked").Combine("a.lua"));
        await this.UntilAsync(() => this.facts.All.Count > 0, () => this.projects.Activate(other.Context)).ConfigureAwait(false);

        // A scan the test awaits analyzes every file it lists, so a file the scan should skip would show by now.
        await this.facts.RefreshAsync(force: false, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = this.facts.All.Should().ContainSingle().Which.Identity.Should().Be(Move);
        _ = this.analyzer.Analyzed.Should().Equal("Content/Player/move.lua");
    }

    [TestMethod]
    public async Task TheSdkApiDefinitionsReachTheAnalyzerWhenTheFileExists()
    {
        _ = this.project.Context.FileSystem.Directory.CreateDirectory(@"C:\Sdk");
        await this.project.Context.FileSystem.File.WriteAllTextAsync(@"C:\Sdk\oxygen.d.luau", "declare oxygen", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = this.project.WriteFile(MoveFile, MoveSource);

        await this.OpenAsync().ConfigureAwait(false);

        _ = this.analyzer.Analyzed.Should().ContainSingle();
        _ = this.analyzer.LastDefinitions.Should().Be("declare oxygen");
    }

    [TestMethod]
    public async Task NoDefinitionsReachTheAnalyzerWhenTheFileIsMissing()
    {
        _ = this.project.WriteFile(MoveFile, MoveSource);

        await this.OpenAsync().ConfigureAwait(false);

        _ = this.analyzer.Analyzed.Should().ContainSingle();
        _ = this.analyzer.LastDefinitions.Should().BeNull();
    }

    [TestMethod]
    public async Task ReadingALibraryScriptWithNoProjectOpenFindsNothing()
    {
        var read = await this.facts.ReadLibraryAsync(AssetPath.Parse("/Engine/Scripts/rotate.oscript"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = read.Should().BeSameAs(ScriptRead.NotFound);
    }

    [TestMethod]
    public async Task ALibraryThatCannotRunReadsAsUnavailable()
    {
        var rotate = AssetPath.Parse("/Engine/Scripts/rotate.oscript");
        this.library.Fail(rotate, new InvalidOperationException("the library is not mounted"));
        this.projects.Activate(this.project.Context);

        var read = await this.facts.ReadLibraryAsync(rotate, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = read.Script.Should().BeNull();
        _ = read.Failure.Should().NotBeNull();
    }

    [TestMethod]
    public async Task OpeningAProjectRaisesChangedForEveryScript()
    {
        var raised = new TaskCompletionSource<ScriptChangedEventArgs>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.facts.Changed += (_, args) => _ = raised.TrySetResult(args);

        this.projects.Activate(this.project.Context);

        var args = await raised.Task.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = args.Script.Should().BeNull();
    }

    private static void WriteRaw(TestProject project, FileSystemPath file)
    {
        _ = project.Context.FileSystem.Directory.CreateDirectory(file.Parent!.FullPath);
        project.Context.FileSystem.File.WriteAllText(file.FullPath, MoveSource);
    }

    private Task OpenAsync()
        => this.UntilAsync(() => this.facts.All.Count > 0, () => this.projects.Activate(this.project.Context));

    private Task ChangeAsync(AssetChange change, Func<bool> done) => this.UntilAsync(done, () => this.changes.OnNext(change));

    // Waits for the facts to publish what the test expects, after the action that makes them.
    private async Task UntilAsync(Func<bool> done, Action act)
    {
        // A channel, not a semaphore: an event already posted can arrive after the wait ends.
        var changed = Channel.CreateUnbounded<bool>();
        void OnChanged(object? sender, ScriptChangedEventArgs args) => _ = changed.Writer.TryWrite(item: true);
        this.facts.Changed += OnChanged;
        try
        {
            act();
            while (!done())
            {
                _ = await changed.Reader.ReadAsync(this.TestContext.CancellationToken).AsTask().WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);
            }
        }
        finally
        {
            this.facts.Changed -= OnChanged;
        }
    }

    private sealed class Catalog(IObservable<AssetChange> changes) : IAssetCatalog
    {
        public IObservable<AssetChange> Changes => changes;

        public Task<IReadOnlyList<AssetRecord>> QueryAsync(AssetQuery query, CancellationToken cancellationToken = default)
            => throw new NotSupportedException();
    }

    // Records what the facts publish; nothing observes it.
    private sealed class RecordingResults(List<OperationResult> published) : IOperationResultPublisher
    {
        private readonly TaskCompletionSource first = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Task FirstPublished => this.first.Task;

        public void Publish(OperationResult result)
        {
            lock (published)
            {
                published.Add(result);
            }

            _ = this.first.TrySetResult();
        }

        public IDisposable Subscribe(IObserver<OperationResult> observer) => Disposable.Empty;
    }

    private sealed class RecordingContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback d, object? state)
            => base.Post(
                callback =>
                {
                    var previous = Current;
                    SetSynchronizationContext(this);
                    try
                    {
                        d(callback);
                    }
                    finally
                    {
                        SetSynchronizationContext(previous);
                    }
                },
                state);
    }
}
