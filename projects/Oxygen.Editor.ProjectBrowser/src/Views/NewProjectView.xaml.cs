// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.ViewModels;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// A page for creating a new project.
/// </summary>
[ViewModel(typeof(NewProjectViewModel))]
public sealed partial class NewProjectView
{
    private readonly IProjectBrowserService projectBrowser;

    /// <summary>
    /// Initializes a new instance of the <see cref="NewProjectView"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    public NewProjectView(IProjectBrowserService projectBrowser)
    {
        this.projectBrowser = projectBrowser;
        this.InitializeComponent();
    }

    /// <summary>
    /// Handles the loaded event of the page.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ViewModel!.LoadTemplates();
    }

    /// <summary>
    /// Handles the event when a template is selected.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnTemplateSelected(object? sender, TemplateItemActivatedEventArgs args)
    {
        _ = sender;

        this.ViewModel!.SelectItem(args.TemplateInfo);
    }

    /// <summary>
    /// Handles the click event of the create button.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void CreateButton_OnClick(object? sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        var template = this.ViewModel!.SelectedItem!;
        var dialog = new NewProjectDialog(this.projectBrowser, template) { XamlRoot = this.XamlRoot };

        var result = await dialog.ShowAsync();

        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel.NewProjectFromTemplate(
                    template,
                    dialog.ViewModel.ProjectName,
                    dialog.ViewModel.SelectedLocation.Path)
                .ConfigureAwait(true);

            if (success)
            {
                Debug.WriteLine("Project successfully created");

                // TODO: navigate to project workspace for newly created project
            }
        }
    }
}
