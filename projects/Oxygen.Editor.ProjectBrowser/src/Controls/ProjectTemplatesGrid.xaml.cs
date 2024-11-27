// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Templates;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A custom control for viewing the project templates in a grid which can be
/// sorted by the last time the template was used.
/// </summary>
public sealed partial class ProjectTemplatesGrid
{
    /// <summary>
    /// The height of a single row in the grid.
    /// </summary>
    public const double SingleRowHeight = 165;

    /// <summary>
    /// The height of the icon in the grid.
    /// </summary>
    public const double IconHeight = 100;

    /// <summary>
    /// The margin for each item in the grid.
    /// </summary>
    public static readonly Thickness ItemMargin = new(20, 20, 20, 20);

    /// <summary>
    /// Identifies the <see cref="SelectedItem"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty SelectedItemProperty = DependencyProperty.Register(
        nameof(SelectedItem),
        typeof(ITemplateInfo),
        typeof(ProjectTemplatesGrid),
        new PropertyMetadata(defaultValue: null, OnSelectedItemChanged));

    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectTemplatesGrid"/> class.
    /// </summary>
    public ProjectTemplatesGrid()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Occurs when an item in the project templates collection is clicked.
    /// </summary>
    [Browsable(true)]
    [Category("Action")]
    [Description("Invoked when user activates a project template tile")]
    public event EventHandler<TemplateItemActivatedEventArgs>? ItemActivated;

    /// <summary>
    /// Gets or sets the collection of project templates.
    /// </summary>
    public ObservableCollection<ITemplateInfo> ProjectTemplates { get; set; } = []; // TODO: This should be a dependency property as it is set in the control

    /// <summary>
    /// Gets or sets the selected item in the grid.
    /// </summary>
    public ITemplateInfo? SelectedItem
    {
        get => (ITemplateInfo)this.GetValue(SelectedItemProperty);
        set => this.SetValue(SelectedItemProperty, value);
    }

    /// <summary>
    /// Handles changes to the <see cref="SelectedItem"/> property.
    /// </summary>
    /// <param name="d">The dependency object.</param>
    /// <param name="e">The event data.</param>
    private static void OnSelectedItemChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((ProjectTemplatesGrid)d).SelectedItem = (ITemplateInfo)e.NewValue;

    /// <summary>
    /// Handles the click event on a grid item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnGridItemClick(object sender, ItemClickEventArgs args)
    {
        _ = sender;

        this.ItemActivated?.Invoke(this, new TemplateItemActivatedEventArgs((ITemplateInfo)args.ClickedItem));
    }
}
