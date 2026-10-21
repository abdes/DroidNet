// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;

namespace Oxygen.Editor.Workspace;

/// <summary>Brings panels forward for the workspace's own notices.</summary>
public partial class WorkspaceViewModel
{
    private void RevealTool(Dockable dockable) => _ = this.placement?.ShowPanelAsync(dockable.Id);
}
