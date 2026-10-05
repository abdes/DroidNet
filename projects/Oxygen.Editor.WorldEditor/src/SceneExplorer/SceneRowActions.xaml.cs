// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// The Scene Explorer row's trailing slots: Show in Editor (eye) and Lock, in two fixed columns that
/// never collapse.
/// </summary>
/// <remarks>
/// Implements the interaction contract's quiet/loud matrix. Default state is quiet: when a node is
/// shown and unlocked, both slots are empty until the pointer enters the row, at which point the eye
/// and unlock affordances appear at muted opacity. A suppressed state is loud: a hidden node pins
/// the eye-off icon and a locked node pins the lock icon permanently, regardless of pointer or
/// selection. Pointer exit removes only the default affordances; a click toggles its own state and
/// never leaves the sibling pinned. Hover subscriptions are attached to the owning row and removed
/// on unload, so they are idempotent under <see cref="ItemsRepeater"/> recycling.
/// </remarks>
public sealed partial class SceneRowActions : UserControl
{
    private const double DefaultAffordanceOpacity = 0.6;
    private const double SuppressedAffordanceOpacity = 1d;

    private DynamicTreeItem? row;
    private LayoutItemAdapter? item;
    private bool hovered;

    /// <summary>Initializes a new instance of the <see cref="SceneRowActions"/> class.</summary>
    public SceneRowActions()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;
    }

    /// <summary>Applies the hover policy without changing cell geometry, so slots never shift.</summary>
    /// <param name="hovered">Whether the owning row currently has the pointer.</param>
    internal void SetRowHovered(bool hovered) => this.RefreshVisibility(hovered);

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        if (this.row is not null)
        {
            return;
        }

        for (DependencyObject? parent = VisualTreeHelper.GetParent(this); parent is not null; parent = VisualTreeHelper.GetParent(parent))
        {
            if (parent is DynamicTreeItem owner)
            {
                this.row = owner;
                owner.AddHandler(UIElement.PointerEnteredEvent, new PointerEventHandler(this.OnRowPointerEntered), handledEventsToo: true);
                owner.AddHandler(UIElement.PointerExitedEvent, new PointerEventHandler(this.OnRowPointerExited), handledEventsToo: true);
                break;
            }
        }

        this.RefreshVisibility(hovered: this.hovered);
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

        this.hovered = false;
        this.RefreshVisibility(hovered: false);
    }

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        _ = sender;
        if (this.item is not null)
        {
            this.item.PropertyChanged -= this.OnItemPropertyChanged;
        }

        this.item = args.NewValue as LayoutItemAdapter;
        if (this.item is not null)
        {
            this.item.PropertyChanged += this.OnItemPropertyChanged;
        }

        this.RefreshVisibility(hovered: this.hovered);
    }

    private void OnItemPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        // A state flip can move a slot between quiet and pinned, so recompute rather than only
        // repainting the glyph.
        _ = sender;
        _ = args;
        this.RefreshVisibility(hovered: this.hovered);
    }

    private void OnRowPointerEntered(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.hovered = true;
        this.RefreshVisibility(hovered: true);
    }

    private void OnRowPointerExited(object sender, PointerRoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.hovered = false;
        this.RefreshVisibility(hovered: false);
    }

    private void RefreshVisibility(bool hovered)
    {
        if (this.item is not { } adapter)
        {
            this.EyeButton.Visibility = Visibility.Collapsed;
            this.LockButton.Visibility = Visibility.Collapsed;
            return;
        }

        // Glyph always mirrors state, so a pinned icon shows the warning variant and a hover-
        // revealed default shows the affordance variant.
        this.EyeGlyph.Glyph = adapter.EditorVisibilityGlyph;
        this.LockGlyph.Glyph = adapter.EditorLockGlyph;

        var (eyeVisible, lockVisible, eyeSuppressed, lockSuppressed) = ComputeSlotVisibility(
            adapter.IsHiddenInEditor,
            adapter.IsLocked,
            hovered);

        this.EyeButton.Visibility = eyeVisible ? Visibility.Visible : Visibility.Collapsed;
        this.LockButton.Visibility = lockVisible ? Visibility.Visible : Visibility.Collapsed;

        // Suppressed states are loud (full opacity warning); hover-revealed defaults are muted
        // affordances. Opacity only — the slot geometry never changes, so icons cannot shift.
        this.EyeButton.Opacity = eyeSuppressed ? SuppressedAffordanceOpacity : DefaultAffordanceOpacity;
        this.LockButton.Opacity = lockSuppressed ? SuppressedAffordanceOpacity : DefaultAffordanceOpacity;
    }

    /// <summary>
    /// Computes the quiet-default / loud-suppressed matrix for the two slots.
    /// </summary>
    /// <param name="hidden">Whether the node is editor-hidden (eye suppressed).</param>
    /// <param name="locked">Whether the node is locked (lock suppressed).</param>
    /// <param name="hovered">Whether the owning row has the pointer.</param>
    /// <returns>
    /// Visibility and suppression flags for the eye and lock slots. A suppressed slot is always
    /// visible and loud; a default slot appears only on hover and is muted. The two slots are
    /// independent, so hiding never pins the lock icon and locking never pins the eye.
    /// </returns>
    public static (bool EyeVisible, bool LockVisible, bool EyeSuppressed, bool LockSuppressed) ComputeSlotVisibility(
        bool hidden,
        bool locked,
        bool hovered)
        => (hidden || hovered, locked || hovered, hidden, locked);

    private void EyeButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.Invoke(static vm => vm.ToggleEditorHiddenCommand, editorHidden: true);
    }

    private void LockButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;
        this.Invoke(static vm => vm.ToggleEditorLockedCommand, editorHidden: false);
    }

    // Commands are reached through the owning tree's view model rather than by writing adapter
    // state here: a row action that flipped IsLocked/IsHiddenInEditor directly would bypass the
    // document command owner and the single undo step it records.
    private void Invoke(System.Func<SceneExplorerViewModel, System.Windows.Input.ICommand> selector, bool editorHidden)
    {
        _ = editorHidden;
        if (this.item is not { } adapter || this.row is null)
        {
            return;
        }

        for (DependencyObject? parent = VisualTreeHelper.GetParent(this.row); parent is not null; parent = VisualTreeHelper.GetParent(parent))
        {
            if (parent is DynamicTree { ViewModel: SceneExplorerViewModel viewModel })
            {
                selector(viewModel).Execute(adapter);
                return;
            }
        }
    }
}
