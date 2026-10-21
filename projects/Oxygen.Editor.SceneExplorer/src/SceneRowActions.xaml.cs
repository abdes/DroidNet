// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>Shows the independent editor-visibility and lock slots of a Scene Explorer node row.</summary>
/// <remarks>
/// XAML owns glyph bindings, commands and visual treatment. This control observes the owning row's
/// pointer lifetime and node state, detaching both subscriptions when its row is recycled.
/// </remarks>
public sealed partial class SceneRowActions : UserControl
{
    /// <summary>Identifies the explorer that owns the row's commands.</summary>
    public static readonly DependencyProperty CommandOwnerProperty = DependencyProperty.Register(
        nameof(CommandOwner),
        typeof(SceneExplorerViewModel),
        typeof(SceneRowActions),
        new PropertyMetadata(defaultValue: null));

    private DynamicTreeItem? row;
    private SceneNodeAdapter? item;
    private bool hovered;

    /// <summary>Initializes a new instance of the <see cref="SceneRowActions"/> class.</summary>
    public SceneRowActions()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;
    }

    /// <summary>Gets the explorer resolved from the owning tree.</summary>
    public SceneExplorerViewModel? CommandOwner => (SceneExplorerViewModel?)this.GetValue(CommandOwnerProperty);

    /// <summary>Updates the row hover lifetime without changing slot geometry.</summary>
    /// <param name="hovered">Whether the pointer is over the owning row.</param>
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

            if (parent is DynamicTree tree)
            {
                this.SetBinding(CommandOwnerProperty, new Binding
                {
                    Source = tree,
                    Path = new PropertyPath(nameof(DynamicTree.ViewModel)),
                    Mode = BindingMode.OneWay,
                });
                break;
            }
        }

        this.ObserveItem(this.DataContext as SceneNodeAdapter);
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

        this.ClearValue(CommandOwnerProperty);
        this.hovered = false;
        this.ObserveItem(adapter: null);
    }

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        _ = sender;
        this.ObserveItem(this.IsLoaded ? args.NewValue as SceneNodeAdapter : null);
    }

    private void ObserveItem(SceneNodeAdapter? adapter)
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
        if (args.PropertyName is null or "" or nameof(LayoutItemAdapter.IsHiddenInEditor) or nameof(LayoutItemAdapter.IsLocked))
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
        var eyeState = this.item?.IsHiddenInEditor == true ? "EyePinned"
            : this.item is not null && this.hovered ? "EyeAffordance" : "EyeQuiet";
        var lockState = this.item?.IsLocked == true ? "LockPinned"
            : this.item is not null && this.hovered ? "LockAffordance" : "LockQuiet";
        _ = VisualStateManager.GoToState(this, eyeState, useTransitions: false);
        _ = VisualStateManager.GoToState(this, lockState, useTransitions: false);
        _ = VisualStateManager.GoToState(this, this.item is not null && this.hovered ? "Interactive" : "Passive", useTransitions: false);
    }
}
