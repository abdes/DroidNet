// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.Input;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Scripting.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.World.Problems;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The Problems panel follows every problem source (the scripts, the input sources, the running scene): grouped by
/// file, counted in its title, cleared when fixed.
/// </summary>
[TestClass]
public sealed partial class ProblemsViewModelTests
{
    private const string MovePath = @"C:\Project\Content\Player\move.lua";
    private static readonly AssetPath Move = AssetPath.Parse("/Content/Player/move.oscript");

    private readonly Mock<ICodeEditorService> codeEditor = new();
    private readonly FakeScripts scripts = new();
    private readonly FakeRuntimeProblems runtime = new();
    private readonly FakeInputSources inputs = new();

    [TestMethod]
    public void AScriptWithProblemsAppearsGroupedByFileWithCountsInTheTitle()
    {
        using var model = this.CreateModel();

        _ = this.scripts.Add(Analyzed(MovePath, Problem(DiagnosticSeverity.Warning, 4), Problem(DiagnosticSeverity.Error, 12)));

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
        _ = this.scripts.Add(Analyzed(MovePath, Problem(DiagnosticSeverity.Error, 12)));

        _ = this.scripts.Add(Analyzed(MovePath));

        _ = model.Groups.Should().BeEmpty();
        _ = model.Title.Should().Be("Problems");
        _ = model.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public void SelectionSurvivesAnUpdateOfAnotherFile()
    {
        using var model = this.CreateModel();
        _ = this.scripts.Add(Analyzed(MovePath, Problem(DiagnosticSeverity.Error, 12)));
        model.Selected = model.Groups[0].Entries[0];

        _ = this.scripts.Add(Analyzed(@"C:\Project\Content\jump.lua", Problem(DiagnosticSeverity.Warning, 2)) with { Identity = AssetPath.Parse("/Content/jump.oscript") });

        _ = model.Selected.Should().NotBeNull();
        _ = model.Selected!.Problem.File.Should().Be(MovePath);
    }

    [TestMethod]
    public async Task OpenGoesToTheProblemLocation()
    {
        using var model = this.CreateModel();
        _ = this.scripts.Add(Analyzed(MovePath, Problem(DiagnosticSeverity.Error, 12)));

        await model.OpenAsync(model.Groups[0].Entries[0]).ConfigureAwait(false);

        this.codeEditor.Verify(editor => editor.OpenAsync(MovePath, 12, 3), Times.Once);
    }

    [TestMethod]
    public void CheckScriptsAnalyzesEveryScriptAgain()
    {
        using var model = this.CreateModel();

        model.CheckScriptsCommand.Execute(parameter: null);

        _ = this.scripts.Refreshes.Should().Equal(true);
    }

    [TestMethod]
    public void RuntimeProblemsJoinTheirFileAndClearWithTheSession()
    {
        using var model = this.CreateModel();
        _ = this.scripts.Add(Analyzed(MovePath, Problem(DiagnosticSeverity.Warning, 4)));

        this.runtime.Report(new RuntimeScriptProblem(DiagnosticSeverity.Error, "S0701", "attempt to index nil", "rendered") { File = MovePath, Line = 9, SlotNode = "Player", SlotIndex = 0 });
        this.runtime.Report(new RuntimeScriptProblem(DiagnosticSeverity.Warning, "OXE.PLAY.SCENE_ISSUE", "unsupported record", "rendered"));

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
        var runtimeProblem = new RuntimeScriptProblem(DiagnosticSeverity.Error, "S0701", "attempt to index nil", "rendered") { SlotNode = "Player", SlotIndex = 0 };
        this.runtime.Report(runtimeProblem);

        model.Selected = model.Groups[0].Entries[0];

        _ = model.CanSelectNode.Should().BeTrue();
        _ = model.OpenSelectedCommand.CanExecute(parameter: null).Should().BeFalse("the problem names no project file");
        await model.SelectNodeCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
        _ = this.runtime.Selected.Should().ContainSingle().Which.Should().Be(runtimeProblem);
    }

    [TestMethod]
    public void AnInputSourcesProblemsAppearUntilItIsFixed()
    {
        using var model = this.CreateModel();
        var problem = new AssetProblem(DiagnosticSeverity.Error, InputSourceCodes.ActionUndeclared, "The source declares no action 'Boost'.")
        {
            File = @"C:\Project\Content\Input\Player.input.json",
            Line = 13,
            Column = 21,
        };

        this.inputs.SetProblems([new InputProblem(problem, AssetPath.Parse("/Content/Input/Player.input"))]);

        var entry = model.Groups.Should().ContainSingle().Which.Entries.Should().ContainSingle().Subject;
        _ = entry.Code.Should().Be(InputSourceCodes.ActionUndeclared);
        _ = entry.Location.Should().Be("13:21");
        _ = entry.Rendered.Should().Contain(InputSourceCodes.ActionUndeclared, "a problem without the engine's rendering is shown in its layout");
        _ = model.ErrorCount.Should().Be(1);

        this.inputs.SetProblems([]);

        _ = model.IsEmpty.Should().BeTrue();
    }

    private static ScriptProblem Problem(DiagnosticSeverity severity, int line)
        => new(severity, "S0503", "key 'fnd_one' not found", "rendered") { File = MovePath, Line = line, Column = 3, Explanation = "explanation" };

    private static Script Analyzed(string path, params ScriptProblem[] problems)
        => new()
        {
            Identity = Move,
            Origin = ScriptOrigin.Source,
            SourcePath = FileSystemPath.From(path),
            Problems = [.. problems.Select(problem => problem with { File = path })],
        };

    private ProblemsViewModel CreateModel()
    {
        var projects = new Mock<IProjectContextService>();
        _ = projects.SetupGet(static service => service.ActiveProject).Returns(TestProject.Create(new MockFileSystem()).Context with { ProjectId = Guid.Empty, Name = "Test", AuthoringMounts = [] });
        return new ProblemsViewModel([this.scripts, this.inputs, this.runtime], this.codeEditor.Object, projects.Object)
        {
            NodeSelector = this.runtime.SelectAsync,
        };
    }

    private sealed class FakeRuntimeProblems : IRuntimeProblemSource
    {
        private readonly List<RuntimeScriptProblem> problems = [];

        public event EventHandler? ProblemsChanged;

        public IReadOnlyList<AssetProblem> Problems => this.problems;

        public List<RuntimeScriptProblem> Selected { get; } = [];

        public DocumentId Document { get; } = DocumentId.New();

        public NodeId Node { get; } = NodeId.New();

        // A problem that names a node is found in the scene that played.
        public (DocumentId Document, NodeId Node)? NodeOf(RuntimeScriptProblem problem)
            => problem.SlotNode is not null ? (this.Document, this.Node) : null;

        public Task<bool> SelectAsync(RuntimeScriptProblem problem)
        {
            this.Selected.Add(problem);
            return Task.FromResult(true);
        }

        public void Report(RuntimeScriptProblem problem)
        {
            this.problems.Add(problem);
            this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
        }

        public void Clear()
        {
            this.problems.Clear();
            this.ProblemsChanged?.Invoke(this, EventArgs.Empty);
        }
    }
}
