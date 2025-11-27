// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

public partial class ToolBar
{
    public static readonly DependencyProperty PrimaryItemsProperty =
        DependencyProperty.Register(
            nameof(PrimaryItems),
            typeof(ObservableCollection<object>),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: null, OnPrimaryItemsChanged));

    public static readonly DependencyProperty SecondaryItemsProperty =
        DependencyProperty.Register(
            nameof(SecondaryItems),
            typeof(ObservableCollection<object>),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: null, OnSecondaryItemsChanged));

    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: false, OnIsCompactPropertyChanged));

    public static readonly DependencyProperty DefaultLabelPositionProperty =
        DependencyProperty.Register(
            nameof(DefaultLabelPosition),
            typeof(ToolBarLabelPosition),
            typeof(ToolBar),
            new PropertyMetadata(ToolBarLabelPosition.Right, OnDefaultLabelPositionPropertyChanged));

    public static readonly DependencyProperty OverflowButtonVisibilityProperty =
        DependencyProperty.Register(
            nameof(OverflowButtonVisibility),
            typeof(Visibility),
            typeof(ToolBar),
            new PropertyMetadata(Visibility.Collapsed));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for ToolBar instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(ToolBar),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    [SuppressMessage("Design", "CA2227:Collection properties should be read only", Justification = "DependencyProperty requires a public setter for XAML.")]
    public ObservableCollection<object> PrimaryItems
    {
        get => (ObservableCollection<object>)this.GetValue(PrimaryItemsProperty);
        set => this.SetValue(PrimaryItemsProperty, value);
    }

    [SuppressMessage("Design", "CA2227:Collection properties should be read only", Justification = "DependencyProperty requires a public setter for XAML.")]
    public ObservableCollection<object> SecondaryItems
    {
        get => (ObservableCollection<object>)this.GetValue(SecondaryItemsProperty);
        set => this.SetValue(SecondaryItemsProperty, value);
    }

    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    public ToolBarLabelPosition DefaultLabelPosition
    {
        get => (ToolBarLabelPosition)this.GetValue(DefaultLabelPositionProperty);
        set => this.SetValue(DefaultLabelPositionProperty, value);
    }

    public Visibility OverflowButtonVisibility
    {
        get => (Visibility)this.GetValue(OverflowButtonVisibilityProperty);
        set => this.SetValue(OverflowButtonVisibilityProperty, value);
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

    private static void OnIsCompactPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnIsCompactChanged();
        }
    }

    private static void OnDefaultLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnDefaultLabelPositionChanged();
        }
    }

    private static void OnPrimaryItemsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnPrimaryItemsCollectionChanged(e.OldValue as ObservableCollection<object>, e.NewValue as ObservableCollection<object>);
        }
    }

    private static void OnSecondaryItemsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnSecondaryItemsCollectionChanged(e.OldValue as ObservableCollection<object>, e.NewValue as ObservableCollection<object>);
        }
    }

    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);
        }
    }

    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<ToolBar>() ?? NullLoggerFactory.Instance.CreateLogger<ToolBar>();
        this.PropagateLoggerFactoryToChildren(loggerFactory);
    }
}
