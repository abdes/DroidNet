// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Windows.System;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

/// <summary>
/// Keeps a results selector and its layout's multi-selection in step, and gives every view the same
/// pointer and keyboard behavior: Ctrl/Shift extend, an empty click clears, a right click selects the
/// clicked row first, Enter and double-click open the active asset.
/// </summary>
internal sealed class AssetSelectionSync
{
    private readonly ListViewBase selector;
    private AssetsLayoutViewModel? model;
    private bool applying;

    /// <summary>Initializes a new instance of the <see cref="AssetSelectionSync"/> class.</summary>
    /// <param name="selector">The view's list or grid.</param>
    public AssetSelectionSync(ListViewBase selector)
    {
        this.selector = selector;
        selector.SelectionMode = ListViewSelectionMode.Extended;
        selector.SelectionChanged += this.OnSelectionChanged;
        selector.KeyDown += this.OnKeyDown;
        selector.DoubleTapped += this.OnDoubleTapped;
        selector.RightTapped += this.OnRightTapped;
        selector.PointerPressed += this.OnPointerPressed;
    }

    /// <summary>Gets or sets the layout whose selection the selector shows.</summary>
    public AssetsLayoutViewModel? Model
    {
        get => this.model;
        set
        {
            if (ReferenceEquals(this.model, value))
            {
                return;
            }

            this.model?.PropertyChanged -= this.OnModelPropertyChanged;
            this.model = value;
            this.model?.PropertyChanged += this.OnModelPropertyChanged;
            this.ApplyFromModel();
        }
    }

    /// <summary>Shows the layout's selection in the selector without reporting it back.</summary>
    public void ApplyFromModel()
    {
        if (this.model is null || !this.selector.IsLoaded)
        {
            return;
        }

        var wanted = this.model.SelectedRows;
        var shown = this.selector.SelectedItems.OfType<AssetBrowserRow>().ToArray();
        if (shown.Length == wanted.Count && shown.All(row => wanted.Contains(row)))
        {
            return;
        }

        this.applying = true;
        try
        {
            this.selector.SelectedItems.Clear();
            foreach (var row in wanted)
            {
                this.selector.SelectedItems.Add(row);
            }
        }
        finally
        {
            this.applying = false;
        }
    }

    private static T? FindAncestor<T>(DependencyObject? element, DependencyObject stop)
        where T : DependencyObject
    {
        for (var current = element; current is not null && !ReferenceEquals(current, stop); current = VisualTreeHelper.GetParent(current))
        {
            if (current is T match)
            {
                return match;
            }
        }

        return null;
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName is nameof(AssetsLayoutViewModel.SelectedRows) or nameof(AssetsLayoutViewModel.SelectedRow))
        {
            this.ApplyFromModel();
        }
    }

    private void OnSelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (this.applying || this.model is not { } current)
        {
            return;
        }

        // Rows still selected keep their order; newly picked rows follow, so the last pick is active.
        var selected = this.selector.SelectedItems.OfType<AssetBrowserRow>().ToHashSet();
        var added = args.AddedItems.OfType<AssetBrowserRow>().ToArray();
        var ordered = current.SelectedRows.Where(row => selected.Contains(row) && !added.Contains(row))
            .Concat(added.Where(selected.Contains))
            .Concat(selected.Where(row => !current.SelectedRows.Contains(row) && !added.Contains(row)));
        current.SetSelection(ordered.Select(static row => row.Item));
    }

    private void OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Key == VirtualKey.Enter && this.model?.SelectedAsset is { } asset)
        {
            this.model.Invoke(asset);
            args.Handled = true;
        }
    }

    private void OnDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (FindAncestor<SelectorItem>(args.OriginalSource as DependencyObject, this.selector) is { } container
            && this.selector.ItemFromContainer(container) is AssetBrowserRow row && this.model is { } current)
        {
            current.Invoke(row.Item);
            args.Handled = true;
        }
    }

    // A context menu acts on what it was opened on: an unselected row becomes the selection.
    private void OnRightTapped(object sender, RightTappedRoutedEventArgs args)
    {
        if (FindAncestor<SelectorItem>(args.OriginalSource as DependencyObject, this.selector) is not { } container
            || this.selector.ItemFromContainer(container) is not AssetBrowserRow row || this.selector.SelectedItems.Contains(row))
        {
            return;
        }

        this.selector.SelectedItems.Clear();
        this.selector.SelectedItems.Add(row);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (FindAncestor<SelectorItem>(args.OriginalSource as DependencyObject, this.selector) is null
            && args.GetCurrentPoint(this.selector).Properties.IsLeftButtonPressed)
        {
            this.selector.SelectedItems.Clear();
        }
    }
}
