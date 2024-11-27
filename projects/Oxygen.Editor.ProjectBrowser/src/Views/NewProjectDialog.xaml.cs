// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// A dialog for creating a new project.
/// </summary>
public sealed partial class NewProjectDialog
{
    /// <summary>
    /// Initializes a new instance of the <see cref="NewProjectDialog"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    /// <param name="template">The template information for the new project.</param>
    public NewProjectDialog(IProjectBrowserService projectBrowser, ITemplateInfo template)
    {
        this.InitializeComponent();

        this.ViewModel = new NewProjectDialogViewModel(projectBrowser, template);
        this.DataContext = this.ViewModel;

        _ = this.ProjectNameTextBox.Focus(FocusState.Programmatic);
    }

    /// <summary>
    /// Gets or sets the view model for the dialog.
    /// </summary>
    public NewProjectDialogViewModel ViewModel { get; set; }

    /// <summary>
    /// Handles the click event of a location item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    private void OnLocationItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender;

        this.ViewModel.SetLocationCommand.Execute(e.ClickedItem);
        this.LocationExpander.IsExpanded = false;
    }
}
