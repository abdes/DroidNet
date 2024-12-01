// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.ViewModels;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// Start Home page.
/// </summary>
[ViewModel(typeof(HomeViewModel))]
public sealed partial class HomeView
{
    private readonly IProjectBrowserService projectBrowser;

    /// <summary>
    /// Initializes a new instance of the <see cref="HomeView"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    public HomeView(IProjectBrowserService projectBrowser)
    {
        this.projectBrowser = projectBrowser;
        this.InitializeComponent();
    }

    /// <summary>
    /// Handles the event when a recent project is opened.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void OnRecentProjectOpenAsync(object sender, ProjectItemActivatedEventArgs args)
    {
        _ = sender;

        var success = await this.ViewModel!.OpenProjectAsync(args.ProjectInfo).ConfigureAwait(false);
        if (!success)
        {
            // TODO: display an error message
        }
    }

    /// <summary>
    /// Handles the event when a new project is created from a template.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void OnNewProjectFromTemplateAsync(object? sender, TemplateItemActivatedEventArgs args)
    {
        _ = sender;

        var dialog = new NewProjectDialog(this.projectBrowser, args.TemplateInfo) { XamlRoot = this.XamlRoot };

        var result = await dialog.ShowAsync();

        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel!.NewProjectFromTemplate(
                    args.TemplateInfo,
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
