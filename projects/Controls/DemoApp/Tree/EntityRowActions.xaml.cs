// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls.Demo.Tree.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>Demo-owned aligned action cells pinned by suppressed state and revealed by row hover.</summary>
/// <remarks>
/// XAML owns the glyphs, commands and visual treatment. This control observes the owning row's
/// pointer lifetime and the item's eye/lock state, and detaches both subscriptions when its row
/// is recycled, so a suppressed state keeps its icon visible regardless of pointer or selection.
/// </remarks>
internal sealed partial class EntityRowActions : UserControl
{
    private DynamicTreeItem? row;
    private EntityAdapter? item;
    private bool hovered;

    /// <summary>Initializes a new instance of the <see cref="EntityRowActions"/> class.</summary>
    public EntityRowActions()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;
    }

    /// <summary>Applies the row hover lifetime to each slot independently without changing cell geometry.</summary>
    /// <param name="hovered">Whether the owning row is hovered.</param>
    internal void SetRowHovered(bool hovered)
    {
        this.hovered = hovered;
        this.UpdateAppearance();
    }

    /// <summary>Updates hover from pointer coordinates relative to the owning row.</summary>
    /// <param name="position">The pointer position in row coordinates.</param>
    internal void UpdateRowPointerPosition(Point position)
        => this.SetRowHovered(this.row is { } owner
            && position.X >= 0 && position.X < owner.ActualWidth
            && position.Y >= 0 && position.Y < owner.ActualHeight);

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        for (var parent = VisualTreeHelper.GetParent(this); parent is not null; parent = VisualTreeHelper.GetParent(parent))
        {
            if (parent is DynamicTreeItem owner && this.row is null)
            {
                this.row = owner;
                owner.AddHandler(UIElement.PointerEnteredEvent, new PointerEventHandler(this.OnRowPointerChanged), handledEventsToo: true);
                owner.AddHandler(UIElement.PointerExitedEvent, new PointerEventHandler(this.OnRowPointerChanged), handledEventsToo: true);
            }

            if (parent is DynamicTree)
            {
                break;
            }
        }

        this.ObserveItem(this.DataContext as EntityAdapter);
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.row is { } owner)
        {
            owner.RemoveHandler(UIElement.PointerEnteredEvent, new PointerEventHandler(this.OnRowPointerChanged));
            owner.RemoveHandler(UIElement.PointerExitedEvent, new PointerEventHandler(this.OnRowPointerChanged));
            this.row = null;
        }

        this.hovered = false;
        this.ObserveItem(null);
    }

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        _ = sender;
        this.Cells.DataContext = args.NewValue as EntityAdapter;
        this.Cells.Visibility = args.NewValue is EntityAdapter ? Visibility.Visible : Visibility.Collapsed;
        this.ObserveItem(this.IsLoaded ? args.NewValue as EntityAdapter : null);
    }

    private void ObserveItem(EntityAdapter? adapter)
    {
        if (this.item is { } previous)
        {
            previous.PropertyChanged -= this.OnItemPropertyChanged;
        }

        this.item = adapter;
        if (this.item is { } current)
        {
            current.PropertyChanged += this.OnItemPropertyChanged;
        }

        this.UpdateAppearance();
    }

    private void OnItemPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender;
        if (args.PropertyName is null or "" or nameof(EntityAdapter.IsVisible) or nameof(EntityAdapter.IsLocked))
        {
            this.UpdateAppearance();
        }
    }

    private void OnRowPointerChanged(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        if (this.row is { } owner)
        {
            // Descendant exit events must not end hover while the pointer is still inside the row.
            this.UpdateRowPointerPosition(args.GetCurrentPoint(owner).Position);
        }
    }

    private void UpdateAppearance()
    {
        var eyeState = this.item?.IsVisible == false ? "EyePinned"
            : this.item is not null && this.hovered ? "EyeAffordance" : "EyeQuiet";
        var lockState = this.item?.IsLocked == true ? "LockPinned"
            : this.item is not null && this.hovered ? "LockAffordance" : "LockQuiet";
        _ = VisualStateManager.GoToState(this, eyeState, useTransitions: false);
        _ = VisualStateManager.GoToState(this, lockState, useTransitions: false);
        _ = VisualStateManager.GoToState(this, this.item is not null && this.hovered ? "Interactive" : "Passive", useTransitions: false);
    }

    private void LockButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.item is { } entity)
        {
            entity.IsLocked = !entity.IsLocked;
        }
    }

    private void VisibilityButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.item is { } entity)
        {
            entity.IsVisible = !entity.IsVisible;
        }
    }
}
