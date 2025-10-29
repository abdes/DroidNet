// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

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
    ///     Gets or sets the TabItem data model for this visual tab.
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
    ///     Gets or sets a value indicating whether this tab item is in compact mode. In compact
    ///     mode, the tool buttons (such as close or pin) are overlaid over the item instead of
    ///     being placed alongside the header.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    private static void OnItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).OnItemChanged(e);

    private static void OnIsCompactChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is TabStripItem tsi)
        {
            tsi.UpdateLayoutForCompactMode();
            tsi.UpdateMinWidth();
        }
    }

    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((TabStripItem)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);

    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
        => this.logger = loggerFactory?.CreateLogger<TabStripItem>();
}
