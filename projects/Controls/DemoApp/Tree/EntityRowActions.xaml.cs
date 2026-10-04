// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Demo-owned aligned action cells whose commands are revealed by row hover.</summary>
internal sealed partial class EntityRowActions : UserControl
{
    private DynamicTreeItem? row;

    /// <summary>Initializes a new instance of the <see cref="EntityRowActions"/> class.</summary>
    public EntityRowActions()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;
    }

    /// <summary>Applies the application-owned hover policy without changing cell geometry.</summary>
    /// <param name="hovered">Whether the owning row is hovered.</param>
    internal void SetRowHovered(bool hovered)
    {
        var visibility = hovered && this.DataContext is EntityAdapter ? Visibility.Visible : Visibility.Collapsed;
        this.LockButton.Visibility = visibility;
        this.VisibilityButton.Visibility = visibility;
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.row is not null)
        {
            return;
        }

        for (var parent = VisualTreeHelper.GetParent(this); parent is not null; parent = VisualTreeHelper.GetParent(parent))
        {
            if (parent is DynamicTreeItem owner)
            {
                this.row = owner;
                owner.AddHandler(UIElement.PointerEnteredEvent, new PointerEventHandler(this.OnRowPointerEntered), handledEventsToo: true);
                owner.AddHandler(UIElement.PointerExitedEvent, new PointerEventHandler(this.OnRowPointerExited), handledEventsToo: true);
                break;
            }
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.row is { } owner)
        {
            owner.RemoveHandler(UIElement.PointerEnteredEvent, new PointerEventHandler(this.OnRowPointerEntered));
            owner.RemoveHandler(UIElement.PointerExitedEvent, new PointerEventHandler(this.OnRowPointerExited));
            this.row = null;
        }

        this.SetRowHovered(hovered: false);
    }

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        _ = sender;
        this.Cells.DataContext = args.NewValue as EntityAdapter;
        this.Cells.Visibility = args.NewValue is EntityAdapter ? Visibility.Visible : Visibility.Collapsed;
        this.SetRowHovered(hovered: false);
    }

    private void OnRowPointerEntered(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.SetRowHovered(hovered: true);
    }

    private void OnRowPointerExited(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.SetRowHovered(hovered: false);
    }

    private void LockButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.DataContext is EntityAdapter item)
        {
            item.IsLocked = !item.IsLocked;
        }
    }

    private void VisibilityButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.DataContext is EntityAdapter item)
        {
            item.IsVisible = !item.IsVisible;
        }
    }
}
