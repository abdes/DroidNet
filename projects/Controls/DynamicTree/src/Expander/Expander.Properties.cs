// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
/// Properties for the <see cref="Expander" /> control.
/// </summary>
[SuppressMessage(
    "ReSharper",
    "ClassWithVirtualMembersNeverInherited.Global",
    Justification = "class is designed to be extended when needed")]
public partial class Expander
{
    /// <summary>
    /// The backing <see cref="DependencyProperty" /> for the <see cref="IsExpanded" /> property.
    /// </summary>
    public static readonly DependencyProperty IsExpandedProperty = DependencyProperty.Register(
        nameof(IsExpanded),
        typeof(bool),
        typeof(Expander),
        new PropertyMetadata(
            default(bool),
            (d, e) => ((Expander)d).OnIsExpandedChanged((bool)e.OldValue, (bool)e.NewValue)));

    /// <summary>
    /// Gets or sets a value indicating whether the <see cref="Expander" /> is in the expanded or collapsed state.
    /// </summary>
    /// <value>
    /// <see langword="true" /> if the <see cref="Expander" /> is expanded; otherwise, <see langword="false" />.
    /// </value>
    /// <remarks>
    /// When the <see cref="IsExpanded" /> property changes, the visual state of the control is updated to reflect the
    /// expanded or collapsed state.
    /// </remarks>
    public bool IsExpanded
    {
        get => (bool)this.GetValue(IsExpandedProperty);
        set => this.SetValue(IsExpandedProperty, value);
    }

    /// <summary>
    /// Called when the <see cref="IsExpanded" /> property changes.
    /// </summary>
    /// <param name="oldValue">The previous value of the <see cref="IsExpanded" /> property.</param>
    /// <param name="newValue">The new value of the <see cref="IsExpanded" /> property.</param>
    protected virtual void OnIsExpandedChanged(bool oldValue, bool newValue)
    {
        Debug.Assert(oldValue != newValue, "expecting SetValue() to not call this method when the value does not change");
        this.UpdateVisualState();
    }
}
