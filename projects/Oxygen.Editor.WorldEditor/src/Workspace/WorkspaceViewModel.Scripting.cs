// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DryIoc;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.World.Problems;
using Oxygen.Editor.World.Scripting;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Script authoring services of the workspace: analysis, the code editor, the Problems panel and project tooling.
/// </summary>
public partial class WorkspaceViewModel
{
    /// <summary>The dock and route identity of the Problems panel.</summary>
    internal const string ProblemsDockId = "problems";

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace's child container, which disposes it.")]
    private ProblemsViewModel? problems;
    private IContainer? scriptContainer;

    private void RegisterScriptServices(IContainer childContainer)
    {
        this.scriptContainer = childContainer;
        childContainer.RegisterInstance(ScriptToolingPaths.Discover());
        childContainer.Register<ICodeEditorService, CodeEditorService>(Reuse.Singleton);
        childContainer.Register<ProjectScriptTooling>(Reuse.Singleton);
        childContainer.Register<IScriptAnalysisService, ScriptAnalysisService>(Reuse.Singleton);
        childContainer.Register<ProblemsViewModel>(Reuse.Singleton);
        childContainer.Register<ProblemsView>(Reuse.Transient);

        // Analysis starts with the workspace so Problems and script badges are populated before the panel is opened.
        this.problems = childContainer.Resolve<ProblemsViewModel>();
        this.problems.PropertyChanged += this.OnProblemsPropertyChanged;
        this.messenger?.Register<ShowScriptProblemMessage>(this, (_, message) => this.ShowProblem(message.Problem));
    }

    // A script slot's badge brings Problems forward at its problem; saves do not, so the code editor keeps focus.
    private void ShowProblem(ContentBrowser.Scripts.ScriptProblem problem)
    {
        _ = this.problems?.Select(problem);
        if (DroidNet.Docking.Dockable.FromId(ProblemsDockId) is { } dock)
        {
            this.RevealTool(dock);
        }
    }

    private void OnScriptingNavigated()
    {
        this.UpdateProblemsTitle();
        if (this.projectContextService.ActiveProject is { } project && this.scriptContainer?.Resolve<ProjectScriptTooling>() is { } tooling)
        {
            _ = Task.Run(() => tooling.Ensure(project));
        }
    }

    private void OnProblemsPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(ProblemsViewModel.Title), StringComparison.Ordinal))
        {
            this.UpdateProblemsTitle();
        }
    }

    private void UpdateProblemsTitle()
    {
        if (DroidNet.Docking.Dockable.FromId(ProblemsDockId) is { } dock && this.problems is { } model)
        {
            dock.Title = model.Title;
        }
    }

    private void DisposeScriptServices()
    {
        if (this.problems is { } model)
        {
            model.PropertyChanged -= this.OnProblemsPropertyChanged;
            this.problems = null;
        }
    }
}
