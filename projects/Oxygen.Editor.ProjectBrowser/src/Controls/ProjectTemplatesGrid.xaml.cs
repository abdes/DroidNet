// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Controls;

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Templates;

/// <summary>
/// A custom control for viewing the project templates in a grid which can be
/// sorted by the last time the template was used.
/// </summary>
public sealed partial class ProjectTemplatesGrid
{
    public const double SingleRowHeight = 165;

    public const double IconHeight = 100;

    public static readonly Thickness ItemMargin = new(20, 20, 20, 20);

    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem),
        typeof(ITemplateInfo),
        typeof(ProjectTemplatesGrid),
        new PropertyMetadata(defaultValue: null, OnSelectedItemChanged));

    public ProjectTemplatesGrid() => this.InitializeComponent();

    /// <summary>Event fired when an item in the project templates collection is clicked.</summary>
    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user clicks a project template tile")]
    public event EventHandler<ITemplateInfo>? ItemClick;

    public ObservableCollection<ITemplateInfo> ProjectTemplates { get; set; } = [];

    public ITemplateInfo? SelectedItem
    {
        get => (ITemplateInfo)this.GetValue(SelectedItemProperty);
        set => this.SetValue(SelectedItemProperty, value);
    }

    private static void OnSelectedItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((ProjectTemplatesGrid)d).SelectedItem = (ITemplateInfo)e.NewValue;

    private void OnGridItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender;

        Debug.WriteLine("Item clicked");
        this.ItemClick?.Invoke(this, (ITemplateInfo)e.ClickedItem);
    }
}
