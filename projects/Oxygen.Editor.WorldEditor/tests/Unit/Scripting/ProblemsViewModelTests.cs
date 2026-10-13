// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Problems;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// The Problems panel follows script analysis: grouped by file, counted in its title, cleared when fixed.
/// </summary>
[TestClass]
public sealed class ProblemsViewModelTests
{
    private const string MovePath = @"C:\Project\Content\Player\move.lua";

    private readonly Subject<ScriptAnalysisSnapshot> changes = new();
    private readonly Mock<ICodeEditorService> codeEditor = new();

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

    private static ScriptProblem Problem(ScriptProblemSeverity severity, int line)
        => new(severity, "S0503", "key 'fnd_one' not found", MovePath, line, 3, "rendered", "explanation");

    private static ScriptAnalysisSnapshot Snapshot(string path, params ScriptProblem[] problems)
        => new(path, new Uri("asset:///Content/x.oscript"), [.. problems.Select(problem => problem with { SourcePath = path })], [], ParametersOutOfDate: false);

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
            Category = Oxygen.Editor.World.Category.Games,
            ProjectRoot = @"C:\Project",
            AuthoringMounts = [],
            LocalFolderMounts = [],
            Scenes = [],
        });
        return new ProblemsViewModel(analysis.Object, this.codeEditor.Object, projects.Object);
    }
}
