// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
/// A view for the World Editor workspace. Simply a docking workspace view.
/// </summary>
public sealed partial class WorkspaceView : DockingWorkspaceView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WorkspaceView"/> class.
    /// </summary>
    public WorkspaceView() => this.InitializeComponent();
}
