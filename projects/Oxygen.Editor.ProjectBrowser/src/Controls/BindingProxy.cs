// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A lightweight binding proxy to expose the ViewModel instance into DataTemplates.
/// </summary>
public sealed class BindingProxy : DependencyObject
{
    /// <summary>
    /// Backing dependency property for the Data property.
    /// </summary>
    public static readonly DependencyProperty DataProperty =
        DependencyProperty.Register("Data", typeof(object), typeof(BindingProxy), new PropertyMetadata(null));

    /// <summary>
    /// Gets or sets an arbitrary object to expose into templates.
    /// </summary>
    public object? Data
    {
        get => this.GetValue(DataProperty);
        set => this.SetValue(DataProperty, value);
    }
}
