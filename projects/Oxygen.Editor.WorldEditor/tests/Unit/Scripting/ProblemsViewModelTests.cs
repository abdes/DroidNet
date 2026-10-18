// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Play;
using Oxygen.Editor.World.Problems;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The Problems panel follows script analysis: grouped by file, counted in its title, cleared when fixed.
/// </summary>
[TestClass]
public sealed partial class ProblemsViewModelTests : IDisposable
{
    private const string MovePath = @"C:\Project\Content\Player\move.lua";

    private readonly Subject<ScriptAnalysisSnapshot> changes = new();
    private readonly Mock<ICodeEditorService> codeEditor = new();
    private readonly FakeRuntimeProblems runtime = new();

    /// <inheritdoc />
    public void Dispose() => this.changes.Dispose();

    [TestMethod]
    public void AnalysisWithProblemsAppearsGroupedByFileWithCountsInTheTitle()
    {
        using var model = this.CreateModel();

        this.changes.OnNext(Snapshot(MovePath, Problem(ScriptProblemSeverity.Warning, 4), Problem(ScriptProblemSeverity.Error, 12)));

        var group = model.Groups.Should().ContainSingle().Subject;
        _ = group.DisplayPath.Should().Be("Content/Player/move.lua");
        _ = group.Entries.Select(static entry => entry.Problem.Line).Should().Equal(4, 12);
        _ = model.Title.Should().Be("Problems  \u26D4 1  \u26A0 1");
        _ = model.IsEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void FixingOrDeletingTheFileClearsItsProblems()
    {
        using var model = this.CreateModel();
        this.changes.OnNext(Snapshot(MovePath, Problem(ScriptProblemSeverity.Error, 12)));

        this.changes.OnNext(Snapshot(MovePath));

        _ = model.Groups.Should().BeEmpty();
        _ = model.Title.Should().Be("Problems");
        _ = model.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public void SelectionSurvivesAnUpdateOfAnotherFile()
    {
        using var model = this.CreateModel();
        this.changes.OnNext(Snapshot(MovePath, Problem(ScriptProblemSeverity.Error, 12)));
        model.Selected = model.Groups[0].Entries[0];

        this.changes.OnNext(Snapshot(@"C:\Project\Content\jump.lua", Problem(ScriptProblemSeverity.Warning, 2)));

        _ = model.Selected.Should().NotBeNull();
        _ = model.Selected!.Problem.SourcePath.Should().Be(MovePath);
    }

    [TestMethod]
    public async Task OpenGoesToTheProblemLocation()
    {
        using var model = this.CreateModel();
        this.changes.OnNext(Snapshot(MovePath, Problem(ScriptProblemSeverity.Error, 12)));

        await model.OpenAsync(model.Groups[0].Entries[0]).ConfigureAwait(false);

        this.codeEditor.Verify(editor => editor.OpenAsync(MovePath, 12, 3), Times.Once);
    }

    [TestMethod]
    public void RuntimeProblemsJoinTheirFileAndClearWithTheSession()
    {
        using var model = this.CreateModel();
        this.changes.OnNext(Snapshot(MovePath, Problem(ScriptProblemSeverity.Warning, 4)));

        this.runtime.Report(Problem(ScriptProblemSeverity.Error, 9) with { IsRuntime = true, SlotNode = "Player", SlotIndex = 0 });
        this.runtime.Report(new ScriptProblem(ScriptProblemSeverity.Warning, "OXE.PLAY.SCENE_ISSUE", "unsupported record", string.Empty, Line: null, Column: null, "rendered", string.Empty) { IsRuntime = true });

        _ = model.Groups.Select(static group => group.DisplayPath).Should().Equal("Content/Player/move.lua", ProblemsViewModel.PlaySessionGroup);
        _ = model.Groups[0].Entries.Select(static entry => entry.Problem.Line).Should().Equal(4, 9);
        _ = model.Title.Should().Be("Problems  \u26D4 1  \u26A0 2");

        this.runtime.Clear();

        _ = model.Groups.Should().ContainSingle().Which.Entries.Should().ContainSingle();
    }

    [TestMethod]
    public async Task ARuntimeProblemSelectsItsNodeAndOnlyAFileProblemOpens()
    {
        using var model = this.CreateModel();
        var runtimeProblem = new ScriptProblem(ScriptProblemSeverity.Error, "S0701", "attempt to index nil", string.Empty, Line: null, Column: null, "rendered", string.Empty)
        {
            IsRuntime = true,
            SlotNode = "Player",
            SlotIndex = 0,
        };
        this.runtime.Report(runtimeProblem);

        model.Selected = model.Groups[0].Entries[0];

        _ = model.CanSelectNode.Should().BeTrue();
        _ = model.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse("the problem names no project file");
        await model.SelectNodeCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = this.runtime.Selected.Should().ContainSingle().Which.Should().Be(runtimeProblem);
    }

    private static ScriptProblem Problem(ScriptProblemSeverity severity, int line)
        => new(severity, "S0503", "key 'fnd_one' not found", MovePath, line, 3, "rendered", "explanation");

    private static ScriptAnalysisSnapshot Snapshot(string path, params ScriptProblem[] problems)
        => new(FileSystemPath.From(path), AssetPath.Parse("/Content/x.oscript"), [.. problems.Select(problem => problem with { SourcePath = path })], [], ParametersOutOfDate: false);

    private ProblemsViewModel CreateModel()
    {
        var analysis = new Mock<IScriptAnalysisService>();
        _ = analysis.SetupGet(static service => service.Changes).Returns(this.changes);
        _ = analysis.SetupGet(static service => service.Current).Returns([]);
        var projects = new Mock<IProjectContextService>();
        _ = projects.SetupGet(static service => service.ActiveProject).Returns(new ProjectContext
        {
            ProjectId = Guid.Empty,
            Name = "Test",
            Category = Oxygen.Editor.Projects.Category.Games,
            ProjectRoot = FileSystemPath.From(@"C:\Project"),
            AuthoringMounts = [],
            LocalFolderMounts = [],
        });
        return new ProblemsViewModel(analysis.Object, this.codeEditor.Object, projects.Object, this.runtime)
        {
            NodeSelector = this.runtime.SelectAsync,
        };
    }

    private sealed class FakeRuntimeProblems : IRuntimeProblemSource
    {
        private readonly List<ScriptProblem> problems = [];

        public event EventHandler? RuntimeProblemsChanged;

        public IReadOnlyList<ScriptProblem> RuntimeProblems => this.problems;

        public List<ScriptProblem> Selected { get; } = [];

        public DocumentId Document { get; } = DocumentId.New();

        public NodeId Node { get; } = NodeId.New();

        // A problem that names a node is found in the scene that played.
        public (DocumentId Document, NodeId Node)? NodeOf(ScriptProblem problem)
            => problem.IsRuntime && problem.SlotNode is not null ? (this.Document, this.Node) : null;

        public Task<bool> SelectAsync(ScriptProblem problem)
        {
            this.Selected.Add(problem);
            return Task.FromResult(true);
        }

        public void Report(ScriptProblem problem)
        {
            this.problems.Add(problem);
            this.RuntimeProblemsChanged?.Invoke(this, EventArgs.Empty);
        }

        public void Clear()
        {
            this.problems.Clear();
            this.RuntimeProblemsChanged?.Invoke(this, EventArgs.Empty);
        }
    }
}
