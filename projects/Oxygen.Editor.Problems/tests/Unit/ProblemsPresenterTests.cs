// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Problems.Unit.Tests.TestSupport;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Problems.Unit.Tests;

/// <summary>The Problems tab shows the counts, and the panel comes forward for a runtime error and for a requested problem.</summary>
[TestClass]
internal sealed class ProblemsPresenterTests : IDisposable
{
    private readonly TestSession session = TestProblems.CreateSession();
    private readonly Mock<IPlacementService> placement = new();
    private readonly FakeRuntimeProblems runtime = new();

    /// <inheritdoc/>
    public void Dispose() => this.session.Dispose();

    /// <summary>The tab shows the title at once, and the title again as the counts change.</summary>
    [TestMethod]
    public void TabTitleFollowsTheCounts()
    {
        using var model = this.CreateModel();
        using var presenter = this.CreatePresenter(model);

        this.runtime.Report(Error());

        this.placement.Verify(service => service.SetPanelTitle(ProblemsModule.PanelId, "Problems"), Times.Once);
        this.placement.Verify(service => service.SetPanelTitle(ProblemsModule.PanelId, ProblemsViewModel.FormatTitle(errors: 1, warnings: 0)), Times.Once);
    }

    /// <summary>A runtime error brings the panel forward once; a warning does not.</summary>
    [TestMethod]
    public void RuntimeErrorBringsThePanelForwardButAWarningDoesNot()
    {
        using var model = this.CreateModel();
        using var presenter = this.CreatePresenter(model);

        this.runtime.Report(new RuntimeScriptProblem(DiagnosticSeverity.Warning, "OXE.PLAY.SCENE_ISSUE", "unsupported record", "rendered"));
        this.placement.Verify(service => service.ShowPanelAsync(ProblemsModule.PanelId, It.IsAny<WindowPlacement>()), Times.Never);

        this.runtime.Report(Error());

        this.placement.Verify(service => service.ShowPanelAsync(ProblemsModule.PanelId, It.IsAny<WindowPlacement>()), Times.Once);
    }

    /// <summary>A request to show a problem selects it and brings the panel forward.</summary>
    /// <returns>A task that completes when the problem was shown.</returns>
    [TestMethod]
    public async Task RequestedProblemIsSelectedAndShown()
    {
        using var model = this.CreateModel();
        using var presenter = this.CreatePresenter(model);
        var problem = Error();
        this.runtime.Report(problem);
        this.placement.Invocations.Clear();
        _ = this.placement.Setup(service => service.ShowPanelAsync(ProblemsModule.PanelId, It.IsAny<WindowPlacement>())).ReturnsAsync(true);

        var shown = await presenter.RevealAsync(problem).ConfigureAwait(false);

        _ = shown.Should().BeTrue();
        _ = model.Selected!.Problem.Should().Be(problem);
        this.placement.Verify(service => service.ShowPanelAsync(ProblemsModule.PanelId, It.IsAny<WindowPlacement>()), Times.Once);
    }

    /// <summary>A disposed presenter no longer follows the panel's counts or the running scene's errors.</summary>
    [TestMethod]
    public void DisposedPresenterNoLongerFollows()
    {
        using var model = this.CreateModel();
        var presenter = this.CreatePresenter(model);
        presenter.Dispose();
        this.placement.Invocations.Clear();

        this.runtime.Report(Error());

        this.placement.VerifyNoOtherCalls();
    }

    private static RuntimeScriptProblem Error()
        => new(DiagnosticSeverity.Error, "S0701", "attempt to index nil", "rendered") { Line = 9, SlotNode = "Player", SlotIndex = 0 };

    private ProblemsViewModel CreateModel()
        => TestProblems.CreateModel(this.session, [this.runtime], new Mock<ICodeEditorService>().Object, this.runtime);

    private ProblemsPresenter CreatePresenter(ProblemsViewModel model)
        => new(model, this.placement.Object, this.runtime);
}
