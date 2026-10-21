// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World.Play;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Play sessions of the workspace: the player runs the scenes of this project and ends with the workspace. The
/// workspace brings Problems forward on a runtime error, and brings forward the panels the play bar asks for.
/// </summary>
public partial class WorkspaceViewModel
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace's child container, which disposes it.")]
    private PlaySession? playSession;
    private int shownRuntimeProblems;

    /// <summary>Brings a panel forward.</summary>
    /// <param name="panel">The panel; <see cref="PlayPanel.None"/> does nothing.</param>
    internal void Reveal(PlayPanel panel)
    {
        if (ScenePlayToolbarViewModel.PanelIdOf(panel) is { } panelId)
        {
            _ = this.placement?.ShowPanelAsync(panelId);
        }
    }

    /// <summary>Shows the scene document a runtime problem names and selects its node.</summary>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>Whether the node was selected.</returns>
    internal async Task<bool> SelectRuntimeProblemNodeAsync(RuntimeScriptProblem problem)
    {
        if (this.playSession?.NodeOf(problem) is not { } target
            || this.documentArea is not { } area
            || area.Documents.FirstOrDefault(document => document.Id == target.Document) is not { Content: SceneContent content } document
            || !await area.ShowAsync(document).ConfigureAwait(true)
            || content.Scene.AllNodes.FirstOrDefault(node => node.Id == target.Node) is not { } node)
        {
            return false;
        }

        this.container.Resolve<ISceneSelectionService>().SetSelection(target.Document, [node], "Problems");
        return true;
    }

    // Registered before Problems, which shows the problems of the running scene, and before Output, which shows its log.
    private void RegisterPlayServices(IContainer childContainer)
    {
        childContainer.Register<PlaySettings>(Reuse.Singleton);
        childContainer.Register<PlaySession>(Reuse.Singleton);
        childContainer.RegisterMapping<IRuntimeProblemSource, PlaySession>();
        childContainer.RegisterMapping<IAssetProblemSource, PlaySession>();
        childContainer.RegisterMapping<IPlayerLog, PlaySession>();
        this.playSession = childContainer.Resolve<PlaySession>();
        this.playSession.ProblemsChanged += this.OnRuntimeProblemsChanged;
    }

    // The player is quit with the project; the connection's end would quit it too.
    private void ShutdownPlayServices()
    {
        if (this.playSession is { } session)
        {
            this.playSession = null;
            session.ProblemsChanged -= this.OnRuntimeProblemsChanged;
            _ = session.ShutdownAsync();
        }
    }

    // A runtime error brings Problems forward; warnings wait to be looked at.
    private void OnRuntimeProblemsChanged(object? sender, EventArgs args)
    {
        var problems = this.playSession?.Problems ?? [];
        var added = problems.Skip(Math.Min(this.shownRuntimeProblems, problems.Count));
        var hasError = problems.Count > this.shownRuntimeProblems && added.Any(static problem => problem.Severity >= DiagnosticSeverity.Error);
        this.shownRuntimeProblems = problems.Count;
        if (hasError)
        {
            this.Reveal(PlayPanel.Problems);
        }
    }
}
