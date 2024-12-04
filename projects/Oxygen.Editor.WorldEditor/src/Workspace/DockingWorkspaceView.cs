// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Workspace;

/// <summary>
/// A custom control to represent the entire docking tree.
/// </summary>
[ViewModel(typeof(DockingWorkspaceViewModel))]
public abstract partial class DockingWorkspaceView : ContentControl
{
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
            this.Content = this.ViewModel.Layout.Content;
            this.ViewModel.Layout.PropertyChanged += this.LayoutPropertyChanged;
        };
        this.Unloaded += (_, _) =>
        {
            Debug.Assert(this.ViewModel?.Layout != null, "The layout should be still set");
            this.ViewModel.Layout.PropertyChanged -= this.LayoutPropertyChanged;
        };
    }

    private void LayoutPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(DockingWorkspaceLayout.Content), StringComparison.Ordinal))
        {
            return;
        }

        this.Content = this.ViewModel!.Layout!.Content;
    }
}
