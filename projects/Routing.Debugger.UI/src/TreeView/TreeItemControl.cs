// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A control that can be used to show any kind of item in a <see cref="TreeView" />.
/// </summary>
public class TreeItemControl : Control
{
    /// <summary>
    /// Defines the <see cref="ItemAdapter" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty ItemAdapterProperty = DependencyProperty.Register(
        nameof(ItemAdapter),
        typeof(ITreeItem),
        typeof(TreeItemControl),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Defines the <see cref="HeaderTemplate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HeaderTemplateProperty = DependencyProperty.Register(
        nameof(HeaderTemplate),
        typeof(DataTemplate),
        typeof(TreeItemControl),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Defines the <see cref="BodyTemplate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty BodyTemplateProperty = DependencyProperty.Register(
        nameof(BodyTemplate),
        typeof(DataTemplate),
        typeof(TreeItemControl),
        new PropertyMetadata(defaultValue: null));

    public TreeItemControl() => this.Style = (Style)Application.Current.Resources[nameof(TreeItemControl)];

    /// <summary>
    /// Gets or sets the template used to display the content of the control's header.
    /// </summary>
    public DataTemplate HeaderTemplate
    {
        get => (DataTemplate)this.GetValue(HeaderTemplateProperty);
        set => this.SetValue(HeaderTemplateProperty, value);
    }

    /// <summary>
    /// Gets or sets the template used to display the content of the control's header.
    /// </summary>
    public DataTemplate BodyTemplate
    {
        get => (DataTemplate)this.GetValue(BodyTemplateProperty);
        set => this.SetValue(BodyTemplateProperty, value);
    }

    /// <summary>
    /// Gets or sets the data used for the header of each control.
    /// </summary>
    public ITreeItem ItemAdapter
    {
        get => (ITreeItem)this.GetValue(ItemAdapterProperty);
        set => this.SetValue(ItemAdapterProperty, value);
    }
}
