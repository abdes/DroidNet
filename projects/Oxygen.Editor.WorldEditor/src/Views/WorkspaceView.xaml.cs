// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Views;

using DroidNet.Mvvm.Generators;
using Oxygen.Editor.WorldEditor.ViewModels;

/// <summary>
/// The World Editor main view provides the primary UI for the user to create and manipulate world scenes and associated
/// entities.
/// </summary>
[ViewModel(typeof(WorkspaceViewModel))]
public sealed partial class WorkspaceView
{
    public WorkspaceView() => this.InitializeComponent();
}
