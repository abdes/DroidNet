// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Properties for the <see cref="Expander" /> control.
/// </summary>
public partial class Expander
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="IsExpanded" /> property.
    /// </summary>
    public static readonly DependencyProperty IsExpandedProperty = DependencyProperty.Register(
        nameof(IsExpanded),
        typeof(bool),
        typeof(Expander),
        new PropertyMetadata(
            defaultValue: false,
            (d, e) => ((Expander)d).OnIsExpandedChanged((bool)e.OldValue, (bool)e.NewValue)));

    /// <summary>
    ///     Identifies the <see cref="LoggerFactory" /> dependency property. Hosts can provide an
    ///     <see cref="ILoggerFactory" /> to enable logging for NumberBox instances.
    /// </summary>
    public static readonly DependencyProperty LoggerFactoryProperty = DependencyProperty.Register(
        nameof(ILoggerFactory),
        typeof(ILoggerFactory),
        typeof(Expander),
        new PropertyMetadata(defaultValue: null, (d, e) => ((Expander)d).OnLoggerFactoryChanged((ILoggerFactory?)e.NewValue)));

    /// <summary>
    ///     Gets or sets a value indicating whether the <see cref="Expander" /> is in the expanded or collapsed state.
    /// </summary>
    /// <value>
    ///     <see langword="true" /> if the <see cref="Expander" /> is expanded; otherwise, <see langword="false" />.
    /// </value>
    /// <remarks>
    ///     When the <see cref="IsExpanded" /> property changes, the visual state of the control is updated to reflect the
    ///     expanded or collapsed state.
    /// </remarks>
    public bool IsExpanded
    {
        get => (bool)this.GetValue(IsExpandedProperty);
        set => this.SetValue(IsExpandedProperty, value);
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
    ///     Called when the <see cref="IsExpanded" /> property changes.
    /// </summary>
    /// <param name="oldValue">The previous value of the <see cref="IsExpanded" /> property.</param>
    /// <param name="newValue">The new value of the <see cref="IsExpanded" /> property.</param>
    protected virtual void OnIsExpandedChanged(bool oldValue, bool newValue)
    {
        Debug.Assert(
            oldValue != newValue,
            "expecting SetValue() to not call this method when the value does not change");
        this.UpdateVisualState();
    }

    // Initialize the logger for this NumberBox. Use the NumberBox type as the category.
    private void OnLoggerFactoryChanged(ILoggerFactory? loggerFactory) =>
        this.logger = loggerFactory?.CreateLogger<Expander>() ?? NullLoggerFactory.Instance.CreateLogger<Expander>();
}
