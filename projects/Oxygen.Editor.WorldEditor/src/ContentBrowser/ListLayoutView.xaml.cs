// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Represents the view for displaying assets in a list layout in the World Editor.
/// </summary>
[ViewModel(typeof(ListLayoutViewModel))]
public sealed partial class ListLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ListLayoutView"/> class.
    /// </summary>
    public ListLayoutView()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Handles the double-tap event on the ListView.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void ListView_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (sender is not ListView { SelectedItem: GameAsset selectedItem })
        {
            return;
        }

        Debug.Assert(this.ViewModel is not null, "view must have a ViewModel");
        this.ViewModel.InvokeItemCommand.Execute(selectedItem);
        args.Handled = true;
    }

    /// <summary>
    /// Handles the pointer pressed event on the ListView.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void ListView_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (sender is not ListView listView)
        {
            return;
        }

        var originalSource = args.OriginalSource as DependencyObject;
        while (originalSource != null && originalSource != listView)
        {
            if (originalSource is ListViewItem)
            {
                return;
            }

            originalSource = VisualTreeHelper.GetParent(originalSource);
        }

        // Clicked outside of any item, clear selection
        listView.SelectedItems.Clear();
    }
}
