// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
/// Provides dependency properties and property change handlers for <see cref="ToolBarToggleButton"/>.
/// </summary>
public partial class ToolBarToggleButton
{
    /// <summary>
    /// Identifies the <see cref="Icon"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IconProperty =
        DependencyProperty.Register(
            nameof(Icon),
            typeof(IconSource),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Identifies the <see cref="Label"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelProperty =
        DependencyProperty.Register(
            nameof(Label),
            typeof(string),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(string.Empty));

    /// <summary>
    /// Identifies the <see cref="IsLabelVisible"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsLabelVisibleProperty =
        DependencyProperty.Register(
            nameof(IsLabelVisible),
            typeof(bool),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(defaultValue: false));

    /// <summary>
    /// Identifies the <see cref="ToolBarLabelPosition"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty LabelPositionProperty =
        DependencyProperty.Register(
            nameof(ToolBarLabelPosition),
            typeof(ToolBarLabelPosition),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(ToolBarLabelPosition.Auto, OnLabelPositionPropertyChanged));

    /// <summary>
    /// Identifies the <see cref="KeyTip"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty KeyTipProperty =
        DependencyProperty.Register(
            nameof(KeyTip),
            typeof(string),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(defaultValue: null, OnKeyTipChanged));

    /// <summary>
    /// Identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(ToolBarToggleButton),
            new PropertyMetadata(defaultValue: false, OnIsCompactPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for ToolBarToggleButton instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(ToolBarToggleButton),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    /// <summary>
    /// Gets or sets the icon displayed by the button.
    /// </summary>
    public IconSource Icon
    {
        get => (IconSource)this.GetValue(IconProperty);
        set => this.SetValue(IconProperty, value);
    }

    /// <summary>
    /// Gets or sets the label text displayed by the button.
    /// </summary>
    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the label is visible.
    /// </summary>
    public bool IsLabelVisible
    {
        get => (bool)this.GetValue(IsLabelVisibleProperty);
        set => this.SetValue(IsLabelVisibleProperty, value);
    }

    /// <summary>
    /// Gets or sets the position of the label relative to the icon.
    /// </summary>
    public ToolBarLabelPosition ToolBarLabelPosition
    {
        get => (ToolBarLabelPosition)this.GetValue(LabelPositionProperty);
        set => this.SetValue(LabelPositionProperty, value);
    }

    /// <summary>
    /// Gets or sets the key tip (keyboard shortcut hint) for the button.
    /// </summary>
    public string KeyTip
    {
        get => (string)this.GetValue(KeyTipProperty);
        set => this.SetValue(KeyTipProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the button is rendered in compact mode.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    ///     Gets or sets the <see cref="ILoggerFactory" /> used to create a logger for this control.
    ///     Assigning the factory will initialize the internal logger to a non-null logger instance
    ///     (falls back to <see cref="NullLoggerFactory.Instance"/> if null).
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>
    /// Handles changes to the <see cref="ToolBarLabelPosition"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarToggleButton toolBarToggleButton)
        {
            toolBarToggleButton.UpdateLabelPosition();
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="IsCompact"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnIsCompactPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarToggleButton toolBarToggleButton)
        {
            toolBarToggleButton.UpdateVisualState();
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="KeyTip"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnKeyTipChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarToggleButton toolBarToggleButton)
        {
            toolBarToggleButton.AccessKey = e.NewValue as string;
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="LoggerFactory"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarToggleButton button)
        {
            button.OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);
        }
    }

    /// <summary>
    /// Updates the internal logger when the <see cref="LoggerFactory"/> property changes.
    /// </summary>
    /// <param name="loggerFactory">The new logger factory.</param>
    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
        => this.logger = loggerFactory?.CreateLogger<ToolBarToggleButton>() ?? NullLoggerFactory.Instance.CreateLogger<ToolBarToggleButton>();
}
