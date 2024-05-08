// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>Start Home page.</summary>
public sealed partial class StartHomePage
{
    public StartHomePage()
    {
        this.InitializeComponent();

        this.ViewModel = Ioc.Default.GetRequiredService<StartHomeViewModel>();
    }

    public StartHomeViewModel ViewModel { get; }

    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        Debug.WriteLine("Navigation to StartHomePage.");

        this.ViewModel.LoadTemplates();
        await this.ViewModel.LoadRecentProjects();
    }

    private void OnProjectClick(object sender, IProjectInfo e)
        => this.ViewModel.OpenProject(e);

    private async void OnTemplateClick(object? sender, ITemplateInfo template)
    {
        var dialog = new NewProjectDialog(template) { XamlRoot = this.XamlRoot };

        var result = await dialog.ShowAsync();

        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel.NewProjectFromTemplate(
                template,
                dialog.ViewModel.ProjectName,
                dialog.ViewModel.SelectedLocation.Path);

            if (success)
            {
                Debug.WriteLine("Project successfully created");

                // TODO(abdes) navigate to project workspace for newly created project
            }
        }
    }
}
