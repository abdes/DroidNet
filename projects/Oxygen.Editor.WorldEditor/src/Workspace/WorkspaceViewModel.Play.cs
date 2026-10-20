// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Docking;
using DryIoc;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Play;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;
using Serilog.Events;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Play sessions of the workspace: the player runs the scenes of this project and ends with the workspace. The
/// workspace shows the player's output in the Output panel, brings Problems forward on a runtime error, and brings
/// forward the panels the play bar asks for.
/// </summary>
public partial class WorkspaceViewModel
{
    private const string PlayerSource = "Player";

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace's child container, which disposes it.")]
    private PlaySession? playSession;
    private OutputLogBuffer? playerOutput;
    private int shownRuntimeProblems;

    /// <summary>Brings a panel forward.</summary>
    /// <param name="panel">The panel; <see cref="PlayPanel.None"/> does nothing.</param>
    internal void Reveal(PlayPanel panel)
    {
        var dockId = panel switch
        {
            PlayPanel.Problems => ProblemsDockId,
            PlayPanel.Cooking => "cook",
            PlayPanel.Output => "log",
            _ => null,
        };
        if (dockId is not null && Dockable.FromId(dockId) is { } dock)
        {
            this.RevealTool(dock);
        }
    }

    /// <summary>Shows the scene document a runtime problem names and selects its node.</summary>
    /// <param name="problem">A problem the running scene reported.</param>
    /// <returns>Whether the node was selected.</returns>
    internal async Task<bool> SelectRuntimeProblemNodeAsync(RuntimeScriptProblem problem)
    {
        if (this.playSession?.NodeOf(problem) is not { } target
            || Dockable.FromId(this.CenterOutletName)?.ViewModel is not DocumentHostViewModel host
            || !await host.SelectDocumentAsync(target.Document).ConfigureAwait(true)
            || this.container.Resolve<OpenDocuments>().Find(target.Document) is not { Content: SceneContent content }
            || content.Scene.AllNodes.FirstOrDefault(node => node.Id == target.Node) is not { } node)
        {
            return false;
        }

        this.container.Resolve<ISceneSelectionService>().SetSelection(target.Document, [node], "Problems");
        return true;
    }

    // Registered before Problems, which shows the problems of the running scene.
    private void RegisterPlayServices(IContainer childContainer)
    {
        childContainer.Register<PlaySettings>(Reuse.Singleton);
        childContainer.Register<PlaySession>(Reuse.Singleton);
        childContainer.RegisterMapping<IRuntimeProblemSource, PlaySession>();
        childContainer.RegisterMapping<IAssetProblemSource, PlaySession>();
        this.playSession = childContainer.Resolve<PlaySession>();
        this.playerOutput = childContainer.Resolve<OutputLogBuffer>(IfUnresolved.ReturnDefault);
        this.playSession.LogReceived += this.OnPlayerLogReceived;
        this.playSession.ProblemsChanged += this.OnRuntimeProblemsChanged;
        this.messenger?.Register<RevealPlayPanelMessage>(this, (_, message) => this.Reveal(message.Panel));
    }

    // The player is quit with the project; the connection's end would quit it too.
    private void ShutdownPlayServices()
    {
        if (this.playSession is { } session)
        {
            this.playSession = null;
            session.LogReceived -= this.OnPlayerLogReceived;
            session.ProblemsChanged -= this.OnRuntimeProblemsChanged;
            _ = session.ShutdownAsync();
        }
    }

    private void OnPlayerLogReceived(object? sender, PlayerLogEventArgs args)
    {
        if (this.playerOutput is not { } output)
        {
            return;
        }

        if (args.Clear)
        {
            output.Clear();
            return;
        }

        output.Append(new OutputLogEntry
        {
            Timestamp = args.Timestamp,
            Level = args.Level switch
            {
                LogLevel.Critical => LogEventLevel.Fatal,
                LogLevel.Error => LogEventLevel.Error,
                LogLevel.Warning => LogEventLevel.Warning,
                LogLevel.Debug => LogEventLevel.Debug,
                LogLevel.Trace => LogEventLevel.Verbose,
                _ => LogEventLevel.Information,
            },
            Source = PlayerSource,
            Channel = args.Channel,
            Message = args.Message,
        });
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
