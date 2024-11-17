// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace DroidNet.Docking.Demo.Workspace;

/// <summary>The view for the docking workspace.</summary>
[ViewModel(typeof(WorkspaceViewModel))]
public sealed partial class WorkspaceView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="WorkspaceView"/> class.
    /// </summary>
    public WorkspaceView()
    {
        this.InitializeComponent();
    }
}
