// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.ContentBrowser.Models;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// Represents the view for displaying assets in a tiles layout in the World Editor.
/// </summary>
[ViewModel(typeof(TilesLayoutViewModel))]
public sealed partial class TilesLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TilesLayoutView"/> class.
    /// </summary>
    public TilesLayoutView()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Handles the double-tap event on the GridView.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void GridView_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (sender is not GridView { SelectedItem: GameAsset selectedItem })
        {
            return;
        }

        Debug.Assert(this.ViewModel is not null, "view must have a ViewModel");
        this.ViewModel.InvokeItemCommand.Execute(selectedItem);
        args.Handled = true;
    }

    /// <summary>
    /// Handles the pointer pressed event on the GridView.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void GridView_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (sender is not GridView gridView)
        {
            return;
        }

        var originalSource = args.OriginalSource as DependencyObject;
        while (originalSource != null && originalSource != gridView)
        {
            if (originalSource is GridViewItem)
            {
                return;
            }

            originalSource = VisualTreeHelper.GetParent(originalSource);
        }

        // Clicked outside of any item, clear selection
        gridView.SelectedItems.Clear();
    }
}
