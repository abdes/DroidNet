// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.TreeView;

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A control to display a tree as a list of expandable items.
/// </summary>
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTreeControl
{
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTreeControl),
        new PropertyMetadata(default(DataTemplateSelector)));

    public DynamicTreeControl()
    {
        this.InitializeComponent();

        this.Style = (Style)Application.Current.Resources[nameof(DynamicTreeControl)];
    }

    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }
}
