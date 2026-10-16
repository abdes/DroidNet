// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DryIoc;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Play;
using Oxygen.Editor.World.SceneExplorer;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Play sessions of the workspace: the player runs the scenes of this project and ends with the workspace.
/// </summary>
public partial class WorkspaceViewModel : IPlayWorkspace
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace's child container, which disposes it.")]
    private PlaySessionService? playSession;

    /// <inheritdoc />
    public Task<bool> SaveAllAsync()
        => Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host ? host.SaveAllAsync() : Task.FromResult(true);

    /// <inheritdoc />
    public void Reveal(PlayPanel panel)
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

    /// <inheritdoc />
    public async Task<bool> SelectNodeAsync(DocumentId sceneDocumentId, SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return Dockable.FromId(this.CenterOutletName)?.ViewModel is DocumentHostViewModel host
            && await host.SelectDocumentAsync(sceneDocumentId).ConfigureAwait(true)
            && Dockable.FromId("se")?.ViewModel is SceneExplorerViewModel explorer
            && await explorer.SetSelectedNodes([node.Id], focusPrimary: true).ConfigureAwait(true);
    }

    // Registered before Problems, which shows the problems of the running scene.
    private void RegisterPlayServices(IContainer childContainer)
    {
        childContainer.Register<PlaySessionSettingsService>(Reuse.Singleton);
        childContainer.Register<PlaySessionService>(Reuse.Singleton);
        childContainer.RegisterMapping<IRuntimeProblemSource, PlaySessionService>();
        this.playSession = childContainer.Resolve<PlaySessionService>();
        this.playSession.AttachWorkspace(this);
    }

    // The player is quit with the project; the connection's end would quit it too.
    private void ShutdownPlayServices()
    {
        if (this.playSession is { } session)
        {
            this.playSession = null;
            _ = session.ShutdownAsync();
        }
    }
}
