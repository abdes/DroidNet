// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>
/// A custom control to show the content created by the <see cref="DockingWorkspaceLayout"/>.
/// </summary>
[ViewModel(typeof(DockingWorkspaceViewModel))]
public abstract partial class DockingWorkspaceView : ContentControl
{
    private bool commandsBound;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockingWorkspaceView"/> class.
    /// </summary>
    protected DockingWorkspaceView()
    {
        this.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        this.VerticalContentAlignment = VerticalAlignment.Stretch;

        this.Loaded += (_, _) =>
        {
            Debug.Assert(this.ViewModel?.Layout != null, "The layout should be set by now");
            var binding = new Binding
            {
                Source = this.ViewModel.Layout,
                Path = new PropertyPath(nameof(DockingWorkspaceLayout.Content)),
                Mode = BindingMode.OneWay,
            };
            this.SetBinding(ContentControl.ContentProperty, binding);

            // The workspace is the window's frame: its keys run the window's commands, wherever the focus is in it.
            if (!this.commandsBound && this.ViewModel.Commands is { } commands)
            {
                CommandKeyBindings.Attach(this, commands);
                this.commandsBound = true;
            }
        };
    }
}
