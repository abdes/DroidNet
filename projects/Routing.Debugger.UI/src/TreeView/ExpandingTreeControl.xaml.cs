// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A control to display a tree as a list of expandable items.
/// </summary>
[ViewModel(typeof(TreeViewModelBase))]
public sealed partial class ExpandingTreeControl : UserControl
{
    /// <summary>
    /// Identifies the <see cref="HeaderTemplate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty HeaderTemplateProperty = DependencyProperty.Register(
        nameof(HeaderTemplate),
        typeof(DataTemplate),
        typeof(TreeItemControl),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Identifies the <see cref="BodyTemplate" /> dependency property.
    /// </summary>
    public static readonly DependencyProperty BodyTemplateProperty = DependencyProperty.Register(
        nameof(BodyTemplate),
        typeof(DataTemplate),
        typeof(TreeItemControl),
        new PropertyMetadata(defaultValue: null));

    public ExpandingTreeControl()
    {
        this.InitializeComponent();
        this.Style = (Style)Application.Current.Resources[nameof(ExpandingTreeControl)];
    }

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
}
