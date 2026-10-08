// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
/// Provides dependency properties and logic for the <see cref="ToolBar"/> control.
/// </summary>
public partial class ToolBar
{
    /// <summary>
    /// Identifies the <see cref="PrimaryItems"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty PrimaryItemsProperty =
        DependencyProperty.Register(
            nameof(PrimaryItems),
            typeof(ObservableCollection<object>),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: null, OnPrimaryItemsChanged));

    /// <summary>
    /// Identifies the <see cref="SecondaryItems"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty SecondaryItemsProperty =
        DependencyProperty.Register(
            nameof(SecondaryItems),
            typeof(ObservableCollection<object>),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: null, OnSecondaryItemsChanged));

    /// <summary>
    /// Identifies the <see cref="IsCompact"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IsCompactProperty =
        DependencyProperty.Register(
            nameof(IsCompact),
            typeof(bool),
            typeof(ToolBar),
            new PropertyMetadata(defaultValue: false, OnIsCompactPropertyChanged));

    /// <summary>
    /// Identifies the <see cref="DefaultLabelPosition"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty DefaultLabelPositionProperty =
        DependencyProperty.Register(
            nameof(DefaultLabelPosition),
            typeof(ToolBarLabelPosition),
            typeof(ToolBar),
            new PropertyMetadata(ToolBarLabelPosition.Right, OnDefaultLabelPositionPropertyChanged));

    /// <summary>
    /// Identifies the <see cref="OverflowButtonVisibility"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty OverflowButtonVisibilityProperty =
        DependencyProperty.Register(
            nameof(OverflowButtonVisibility),
            typeof(Visibility),
            typeof(ToolBar),
            new PropertyMetadata(Visibility.Collapsed));

    /// <summary>
    /// Identifies the <see cref="LoggerFactory"/> dependency property. Hosts can provide an
    /// <see cref="ILoggerFactory"/> to enable logging for ToolBar instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(LoggerFactory),
        typeof(ILoggerFactory),
        typeof(ToolBar),
        new PropertyMetadata(defaultValue: null, OnLoggerFactoryChanged));

    /// <summary>
    /// Identifies the OverflowPriority attached property. When primary items do not fit, items with a
    /// lower priority move to the overflow menu first; equal priorities overflow from the right.
    /// </summary>
    public static readonly DependencyProperty OverflowPriorityProperty = DependencyProperty.RegisterAttached(
        "OverflowPriority",
        typeof(int),
        typeof(ToolBar),
        new PropertyMetadata(0, OnOverflowPriorityChanged));

    /// <summary>
    /// Gets or sets the collection of primary items displayed in the toolbar.
    /// </summary>
    [SuppressMessage("Design", "CA2227:Collection properties should be read only", Justification = "DependencyProperty requires a public setter for XAML.")]
    public ObservableCollection<object> PrimaryItems
    {
        get => (ObservableCollection<object>)this.GetValue(PrimaryItemsProperty);
        set => this.SetValue(PrimaryItemsProperty, value);
    }

    /// <summary>
    /// Gets or sets the collection of secondary items displayed in the toolbar.
    /// </summary>
    [SuppressMessage("Design", "CA2227:Collection properties should be read only", Justification = "DependencyProperty requires a public setter for XAML.")]
    public ObservableCollection<object> SecondaryItems
    {
        get => (ObservableCollection<object>)this.GetValue(SecondaryItemsProperty);
        set => this.SetValue(SecondaryItemsProperty, value);
    }

    /// <summary>
    /// Gets or sets a value indicating whether the toolbar is rendered in compact mode.
    /// </summary>
    public bool IsCompact
    {
        get => (bool)this.GetValue(IsCompactProperty);
        set => this.SetValue(IsCompactProperty, value);
    }

    /// <summary>
    /// Gets or sets the default label position for items in the toolbar.
    /// </summary>
    public ToolBarLabelPosition DefaultLabelPosition
    {
        get => (ToolBarLabelPosition)this.GetValue(DefaultLabelPositionProperty);
        set => this.SetValue(DefaultLabelPositionProperty, value);
    }

    /// <summary>
    /// Gets or sets the visibility of the overflow button.
    /// </summary>
    public Visibility OverflowButtonVisibility
    {
        get => (Visibility)this.GetValue(OverflowButtonVisibilityProperty);
        set => this.SetValue(OverflowButtonVisibilityProperty, value);
    }

    /// <summary>
    /// Gets or sets the <see cref="ILoggerFactory"/> used to create a logger for this control.
    /// Assigning the factory will initialize the internal logger to a non-null logger instance
    /// (falls back to <see cref="NullLoggerFactory.Instance"/> if null).
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => (ILoggerFactory?)this.GetValue(LoggerFactoryProperty);
        set => this.SetValue(LoggerFactoryProperty, value);
    }

    /// <summary>Gets the overflow priority of a toolbar item.</summary>
    /// <param name="element">The toolbar item.</param>
    /// <returns>The item's priority; higher priorities stay visible longer.</returns>
    public static int GetOverflowPriority(DependencyObject element)
    {
        ArgumentNullException.ThrowIfNull(element);
        return (int)element.GetValue(OverflowPriorityProperty);
    }

    /// <summary>Sets the overflow priority of a toolbar item.</summary>
    /// <param name="element">The toolbar item.</param>
    /// <param name="value">The priority; higher priorities stay visible longer.</param>
    public static void SetOverflowPriority(DependencyObject element, int value)
    {
        ArgumentNullException.ThrowIfNull(element);
        element.SetValue(OverflowPriorityProperty, value);
    }

    private static void OnOverflowPriorityChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is UIElement element && FindToolBar(element) is { } toolBar)
        {
            toolBar.UpdateOverflow();
        }
    }

    private static ToolBar? FindToolBar(DependencyObject element)
    {
        for (var current = VisualTreeHelper.GetParent(element); current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is ToolBar toolBar)
            {
                return toolBar;
            }
        }

        return null;
    }

    /// <summary>
    /// Handles changes to the <see cref="IsCompact"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnIsCompactPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnIsCompactChanged();
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="DefaultLabelPosition"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnDefaultLabelPositionPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnDefaultLabelPositionChanged();
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="PrimaryItems"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnPrimaryItemsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnPrimaryItemsCollectionChanged(e.OldValue as ObservableCollection<object>, e.NewValue as ObservableCollection<object>);
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="SecondaryItems"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnSecondaryItemsChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnSecondaryItemsCollectionChanged(e.OldValue as ObservableCollection<object>, e.NewValue as ObservableCollection<object>);
        }
    }

    /// <summary>
    /// Handles changes to the <see cref="LoggerFactory"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnLoggerFactoryChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is ToolBar toolBar)
        {
            toolBar.OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue);
        }
    }

    /// <summary>
    /// Updates the internal logger when the <see cref="LoggerFactory"/> property changes.
    /// </summary>
    /// <param name="loggerFactory">The new logger factory.</param>
    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger<ToolBar>() ?? NullLoggerFactory.Instance.CreateLogger<ToolBar>();
        this.PropagateLoggerFactoryToChildren(loggerFactory);
    }
}
