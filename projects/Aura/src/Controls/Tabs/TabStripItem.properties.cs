// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
public partial class TabStripItem
{
    /// <summary>
    ///     Identifies the <see cref="Item"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemProperty =
        DependencyProperty.Register(
            nameof(Item),
            typeof(TabItem),
            typeof(TabStripItem),
            new PropertyMetadata(defaultValue: null, OnItemChanged));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty =
        DependencyProperty.Register(
            nameof(LoggerFactory),
            typeof(ILoggerFactory),
            typeof(TabStripItem),
            new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    /// <summary>
    ///     Identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(TabStripItem),
            new PropertyMetadata(defaultValue: false, OnIsCompactChanged));

    /// <summary>
    ///     Identifies the <see cref="IsDragging"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsDraggingProperty =
        DependencyProperty.Register(
            nameof(IsDragging),
            typeof(bool),
            typeof(TabStripItem),
            new PropertyMetadata(defaultValue: false, OnIsDraggingChanged));

    /// <summary>
    ///     Gets or sets identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public TabItem? Item
    {
        get => (TabItem?)this.GetValue(ItemProperty);
        set => this.SetValue(ItemProperty, value);
    }

    /// <summary>
    ///     Gets or sets the logger factory for this tab item.
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether this tab item is in compact mode. This may
    ///     trigger specific layout management in the control, such as compacting the header,
    ///     overlaying the tool buttons, etc.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    ///     Gets a value indicating whether this tab item is currently being dragged. When a drag
    ///     begins, this property is set to <see langword="true"/>, and templates can bind to it to
    ///     show drag visual feedback. The property is reset to <see langword="false"/> when the
    ///     drag ends. This property is typically set only by the <see cref="TabStrip"/> during drag
    ///     lifecycle operations and should not be set directly by external code.
    /// </summary>
    public bool IsDragging
    {
        get => (bool)this.GetValue(IsDraggingProperty);
        internal set => this.SetValue(IsDraggingProperty, value);
    }

    private static void OnItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).OnItemChanged(e);

    private static void OnIsCompactChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).OnIsCompactChanged(e.OldValue is bool oldValue && oldValue, e.NewValue is bool newValue && newValue);

    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);

    private static void OnIsDraggingChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).UpdateVisualStates(useTransitions: true);

    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
        => this.logger = loggerFactory?.CreateLogger<TabStripItem>();
}
