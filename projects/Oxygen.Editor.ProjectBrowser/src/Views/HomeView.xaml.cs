// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>Start Home page.</summary>
[ViewModel(typeof(HomeViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class HomeView
{
    public HomeView() => this.InitializeComponent();

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ViewModel?.LoadTemplates();
        this.ViewModel!.LoadRecentProjects();
    }

    private void OnProjectClick(object sender, IProjectInfo e)
    {
        _ = sender;

        this.ViewModel!.OpenProject(e);
    }

    private async void OnTemplateClick(object? sender, ITemplateInfo template)
    {
        _ = sender;

        var dialog = new NewProjectDialog(template) { XamlRoot = this.XamlRoot };

        var result = await dialog.ShowAsync();

        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel!.NewProjectFromTemplate(
                    template,
                    dialog.ViewModel.ProjectName,
                    dialog.ViewModel.SelectedLocation.Path)
                .ConfigureAwait(true);

            if (success)
            {
                Debug.WriteLine("Project successfully created");

                // TODO(abdes) navigate to project workspace for newly created project
            }
        }
    }
}
