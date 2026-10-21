// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using System.Threading.Channels;
using AwesomeAssertions;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Input.Tests;

/// <summary>
/// The input facts hold the session project's saved input sources and every problem their cook would reject, and follow
/// each change of a source's file and of the content mounts.
/// </summary>
/// <remarks>
/// The session starts with the project mounting no content; a test opens the project's sources by giving it its
/// <c>Content</c> mount, as a mount change does, after it has subscribed to what the facts raise.
/// </remarks>
[TestClass]
public sealed partial class InputFactsTests : IDisposable
{
    private const string PlayerFile = "/Content/Input/Player.input.json";
    private const string CommonFile = "/Content/Input/Common.input.json";

    private static readonly AssetPath Player = AssetPath.Parse("/Content/Input/Player.input");
    private static readonly TimeSpan Patience = TimeSpan.FromSeconds(10);

    private readonly TestProject project = TestProject.Create(new MockFileSystem());
    private readonly TestSession session;
    private readonly Subject<AssetChange> changes = new();
    private readonly InputFacts facts;

    public InputFactsTests()
    {
        this.session = new TestSession(this.project.Context with { AuthoringMounts = [] });
        this.facts = new InputFacts(this.session, new Catalog(this.changes));
    }

    public TestContext TestContext { get; set; }

    public void Dispose()
    {
        this.facts.Dispose();
        this.session.Dispose();
        this.changes.Dispose();
    }

    [TestMethod]
    public async Task TheSourcesOfAProjectAreReadWhenItOpens()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));

        await this.OpenAsync().ConfigureAwait(false);

        _ = this.facts.All.Should().ContainSingle().Which.Identity.Should().Be(Player);
        _ = this.facts.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task AProblemNamesItsSourceAndWhereItIs()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player").Replace("\"action\": \"Jump\"", "\"action\": \"Boost\"", StringComparison.Ordinal));

        await this.OpenAsync().ConfigureAwait(false);

        var problem = this.facts.Problems.Should().ContainSingle().Which.Should().BeOfType<InputProblem>().Subject;
        _ = problem.Code.Should().Be(InputSourceCodes.ActionUndeclared);
        _ = problem.Container.Should().Be(Player);
        _ = problem.File.Should().Be(this.project.PathOf(PlayerFile).FullPath);
        _ = problem.Line.Should().Be(13);
    }

    [TestMethod]
    public async Task ASourceThatDoesNotReadHasProblemsAndNoModel()
    {
        _ = this.project.WriteFile(PlayerFile, "{\n  \"actions\": [\n");
        _ = this.project.WriteFile(CommonFile, """{ "actions": 3 }""");

        await this.OpenAsync().ConfigureAwait(false);

        _ = this.facts.All.Should().BeEmpty();
        _ = this.facts.Problems.Select(static problem => problem.Code).Distinct().Should().BeEquivalentTo(InputSourceCodes.NotJson, InputSourceCodes.SchemaMismatch);
    }

    [TestMethod]
    public async Task AChangedSourceIsReadAgain()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);

        _ = this.project.WriteFile(PlayerFile, "not json");
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Updated, AssetPath.Parse(PlayerFile)), () => this.facts.Problems.Count == 1).ConfigureAwait(false);

        _ = this.facts.Problems[0].Code.Should().Be(InputSourceCodes.NotJson);
        _ = this.facts.Find(Player).Should().BeNull();
    }

    [TestMethod]
    public async Task ACheckReadsASourceWhoseFileChangedSinceItWasRead()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);

        // The file changed, and its change has not reached the facts yet.
        _ = this.project.WriteFile(PlayerFile, "not json");
        this.facts.Check([AssetPath.Parse(PlayerFile), AssetPath.Parse("/Content/Scripts/Orbit.lua")]);

        _ = this.facts.Problems.Should().ContainSingle().Which.Code.Should().Be(InputSourceCodes.NotJson);
    }

    [TestMethod]
    public async Task ACheckKeepsAnUnchangedSourceAsItWasRead()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);
        var read = this.facts.Find(Player);

        this.facts.Check([AssetPath.Parse(PlayerFile)]);

        _ = this.facts.Find(Player).Should().BeSameAs(read);
    }

    [TestMethod]
    public async Task DeclaringAnActionClearsTheProblemsOfTheSourcesThatNameIt()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player").Replace("\"action\": \"Jump\"", "\"action\": \"/Content/Input/Common.input/Fire.oiact\"", StringComparison.Ordinal));
        await this.OpenAsync().ConfigureAwait(false);
        _ = this.facts.Problems.Should().ContainSingle().Which.Code.Should().Be(InputSourceCodes.ActionMissing);

        _ = this.project.WriteFile(CommonFile, """{ "actions": [ { "name": "Fire", "type": "bool" } ] }""");
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Added, AssetPath.Parse(CommonFile)), () => this.facts.Problems.Count == 0).ConfigureAwait(false);

        _ = this.facts.All.Should().HaveCount(2);
    }

    [TestMethod]
    public async Task ARemovedSourceIsForgotten()
    {
        var file = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);

        this.project.Context.FileSystem.File.Delete(file.FullPath);
        await this.ChangeAsync(new AssetChange(AssetChangeKind.Removed, AssetPath.Parse(PlayerFile)), () => this.facts.All.Count == 0).ConfigureAwait(false);

        _ = this.facts.Find(Player).Should().BeNull();
    }

    [TestMethod]
    public async Task AMovedSourceIsFoundAtItsNewIdentity()
    {
        var file = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);

        this.project.Context.FileSystem.File.Move(file.FullPath, this.project.PathOf("/Content/Input/Hero.input.json").FullPath);
        await this.ChangeAsync(
            new AssetChange(AssetChangeKind.Relocated, AssetPath.Parse("/Content/Input/Hero.input.json"), AssetPath.Parse(PlayerFile)),
            () => this.facts.Find(Player) is null).ConfigureAwait(false);

        _ = this.facts.All.Should().ContainSingle().Which.Identity.Should().Be(AssetPath.Parse("/Content/Input/Hero.input"));
    }

    [TestMethod]
    public async Task UnmountingTheContentForgetsItsSources()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        await this.OpenAsync().ConfigureAwait(false);

        this.session.ChangeContext(this.project.Context with { AuthoringMounts = [] });

        _ = this.facts.All.Should().BeEmpty();
        _ = this.facts.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public async Task EventsAreRaisedOnTheSubscribersContext()
    {
        _ = this.project.WriteFile(PlayerFile, InputTemplates.New("Player"));
        var context = new RecordingContext();
        var raised = new TaskCompletionSource<SynchronizationContext?>(TaskCreationOptions.RunContinuationsAsynchronously);
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(context);
        try
        {
            this.facts.ProblemsChanged += (_, _) => raised.TrySetResult(SynchronizationContext.Current);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }

        this.session.ChangeContext(this.project.Context);

        _ = (await raised.Task.WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeSameAs(context);
    }

    [TestMethod]
    public async Task TheKitFakeReadsASourceAsTheFactsDo()
    {
        var json = InputTemplates.New("Player");
        _ = this.project.WriteFile(PlayerFile, json);
        await this.OpenAsync().ConfigureAwait(false);

        var fake = new FakeInputSources();
        _ = fake.Add(Player.VirtualPath, json);

        var real = this.facts.Find(Player)!;
        var faked = fake.Find(Player)!;
        _ = faked.Actions.Should().Equal(real.Actions);
        _ = faked.Contexts.Should().Equal(
            real.Contexts,
            static (actual, expected) => actual.Identity == expected.Identity && actual.Priority == expected.Priority && actual.Declaration.Position == expected.Declaration.Position);
    }

    private Task OpenAsync()
        => this.UntilAsync(() => this.facts.All.Count > 0 || this.facts.Problems.Count > 0, () => this.session.ChangeContext(this.project.Context));

    private Task ChangeAsync(AssetChange change, Func<bool> done) => this.UntilAsync(done, () => this.changes.OnNext(change));

    // Waits for the facts to publish what the test expects, after the action that makes them.
    private async Task UntilAsync(Func<bool> done, Action act)
    {
        // A channel, not a semaphore: an event already posted can arrive after the wait ends.
        var published = Channel.CreateUnbounded<bool>();
        void OnChanged(object? sender, InputSourceChangedEventArgs args) => _ = published.Writer.TryWrite(item: true);
        this.facts.Changed += OnChanged;
        try
        {
            act();
            while (!done())
            {
                _ = await published.Reader.ReadAsync(this.TestContext.CancellationToken).AsTask().WaitAsync(Patience, this.TestContext.CancellationToken).ConfigureAwait(false);
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
