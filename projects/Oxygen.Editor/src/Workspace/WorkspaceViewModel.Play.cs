// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Play sessions of the workspace: the player runs the scenes of this project and ends with the workspace. The
/// workspace brings forward the panels the play bar asks for.
/// </summary>
public partial class WorkspaceViewModel
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace's child container, which disposes it.")]
    private PlaySession? playSession;

    // Registered before Problems, which shows the problems of the running scene, and before Output, which shows its log.
    private void RegisterPlayServices(IContainer childContainer)
    {
        childContainer.Register<PlaySettings>(Reuse.Singleton);
        childContainer.Register<PlaySession>(Reuse.Singleton);
        childContainer.RegisterMapping<IRuntimeProblemSource, PlaySession>();
        childContainer.RegisterMapping<IAssetProblemSource, PlaySession>();
        childContainer.RegisterMapping<IPlayerLog, PlaySession>();
        this.playSession = childContainer.Resolve<PlaySession>();
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
