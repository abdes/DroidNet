// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

public partial class ToolBarButton
{
    public static readonly DependencyProperty IconProperty =
        DependencyProperty.Register(
            nameof(Icon),
            typeof(IconSource),
            typeof(ToolBarButton),
            new PropertyMetadata(defaultValue: null));

    public static readonly DependencyProperty LabelProperty =
        DependencyProperty.Register(
            nameof(Label),
            typeof(string),
            typeof(ToolBarButton),
            new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty IsLabelVisibleProperty =
        DependencyProperty.Register(
            nameof(IsLabelVisible),
            typeof(bool),
            typeof(ToolBarButton),
            new PropertyMetadata(defaultValue: false));

    public static readonly DependencyProperty LabelPositionProperty =
        DependencyProperty.Register(
            nameof(ToolBarLabelPosition),
            typeof(ToolBarLabelPosition),
            typeof(ToolBarButton),
            new PropertyMetadata(ToolBarLabelPosition.Auto, OnLabelPositionPropertyChanged));

    public static readonly DependencyProperty KeyTipProperty =
        DependencyProperty.Register(
            nameof(KeyTip),
            typeof(string),
            typeof(ToolBarButton),
            new PropertyMetadata(defaultValue: null, OnKeyTipChanged));

    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(ToolBarButton),
            new PropertyMetadata(defaultValue: false, OnIsCompactPropertyChanged));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for ToolBarButton instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(ToolBarButton),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    public IconSource Icon
    {
        get => (IconSource)this.GetValue(IconProperty);
        set => this.SetValue(IconProperty, value);
    }

    public string Label
    {
        get => (string)this.GetValue(LabelProperty);
        set => this.SetValue(LabelProperty, value);
    }

    public bool IsLabelVisible
    {
        get => (bool)this.GetValue(IsLabelVisibleProperty);
        set => this.SetValue(IsLabelVisibleProperty, value);
    }

    public ToolBarLabelPosition ToolBarLabelPosition
    {
        get => (ToolBarLabelPosition)this.GetValue(LabelPositionProperty);
        set => this.SetValue(LabelPositionProperty, value);
    }

    public string KeyTip
    {
        get => (string)this.GetValue(KeyTipProperty);
        set => this.SetValue(KeyTipProperty, value);
    }

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

    private static void OnLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarButton toolBarButton)
        {
            toolBarButton.UpdateLabelPosition();
        }
    }

    private static void OnIsCompactPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarButton toolBarButton)
        {
            toolBarButton.UpdateVisualState();
        }
    }

    private static void OnKeyTipChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarButton toolBarButton)
        {
            toolBarButton.AccessKey = e.NewValue as string;
        }
    }

    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBarButton button)
        {
            button.OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);
        }
    }

    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
        => this.logger = loggerFactory?.CreateLogger<ToolBarButton>() ?? NullLoggerFactory.Instance.CreateLogger<ToolBarButton>();
}
