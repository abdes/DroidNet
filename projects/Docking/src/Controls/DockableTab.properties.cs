// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Docking.Controls;

/// <summary>
/// Represents a tab within a dock panel dockable tabs control that can display an icon and a title
/// for the corresponding <see cref="Dockable"/> view.
/// </summary>
public sealed partial class DockableTab
{
    /// <summary>
    /// Identifies the <see cref="Dockable"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DockableProperty =
        DependencyProperty.Register(
            nameof(Dockable),
            typeof(IDockable),
            typeof(DockableTab),
            new PropertyMetadata(defaultValue: null, OnDockableChanged));

    /// <summary>
    /// Identifies the <see cref="IconConverter"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IconConverterProperty =
        DependencyProperty.Register(
            nameof(IconConverter),
            typeof(IValueConverter),
            typeof(DockableTab),
            new PropertyMetadata(defaultValue: null, OnIconConverterChanged));

    /// <summary>
    /// Identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(DockableTab),
            new PropertyMetadata(defaultValue: false));

    /// <summary>
    /// Identifies the <see cref="TextTrimming"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty TextTrimmingProperty =
        DependencyProperty.Register(
            nameof(TextTrimming),
            typeof(TextTrimming),
            typeof(DockableTab),
            new PropertyMetadata(TextTrimming.None));

    /// <summary>
    /// Identifies the <see cref="IsSelected"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsSelectedProperty =
        DependencyProperty.Register(
            nameof(IsSelected),
            typeof(bool),
            typeof(DockableTab),
            new PropertyMetadata(defaultValue: false, OnIsSelectedChanged));

    /// <summary>
    /// Gets or sets the dockable entity associated with this tab.
    /// </summary>
    public IDockable Dockable
    {
        get => (IDockable)this.GetValue(DockableProperty);
        set => this.SetValue(DockableProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the tab is selected.
    /// </summary>
    public bool IsSelected
    {
        get => (bool)this.GetValue(IsSelectedProperty);
        set => this.SetValue(IsSelectedProperty, value);
    }

    /// <summary>
    /// Gets or sets the converter used to get an icon for the dockable entity.
    /// </summary>
    public IValueConverter IconConverter
    {
        get => (IValueConverter)this.GetValue(IconConverterProperty);
        set => this.SetValue(IconConverterProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the tab is shown with no title.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    /// Gets or sets the text trimming behavior for the title.
    /// </summary>
    public TextTrimming TextTrimming
    {
        get => (TextTrimming)this.GetValue(TextTrimmingProperty);
        set => this.SetValue(TextTrimmingProperty, value);
    }

    /// <summary>
    /// Called when the <see cref="Dockable"/> property changes.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="args">The event data.</param>
    private static void OnDockableChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
        => ((DockableTab)d).UpdateIcon();

    /// <summary>
    /// Called when the <see cref="IconConverter"/> property changes.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="args">The event data.</param>
    private static void OnIconConverterChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
        => ((DockableTab)d).UpdateIcon();

    /// <summary>
    /// Called when the <see cref="IsSelected"/> property changes.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="args">The event data.</param>
    private static void OnIsSelectedChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
        => ((DockableTab)d).UpdateSelectionState();
}
