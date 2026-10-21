// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.LivePreview.Play;

namespace Oxygen.Editor.Workspace;

/// <summary>
/// Play sessions of the workspace: the player runs the scenes of this project and ends with the workspace. The
/// workspace brings forward the panels the play bar asks for.
/// </summary>
public partial class WorkspaceViewModel
{
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "A singleton of the workspace window's scope, which disposes it.")]
    private PlaySession? playSession;

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
