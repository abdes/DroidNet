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
using Oxygen.Editor.ProjectBrowser.Controls;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>Start Home page.</summary>
[ViewModel(typeof(HomeViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class HomeView
{
    public HomeView() => this.InitializeComponent();

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        await this.ViewModel!.LoadTemplatesCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await this.ViewModel!.LoadRecentProjectsCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
    }

    private async void OnRecentProjectOpenAsync(object sender, RecentProjectsList.ItemActivatedEventArgs args)
    {
        _ = sender;

        var success = await this.ViewModel!.OpenProjectAsync(args.ProjectInfo).ConfigureAwait(false);
        if (!success)
        {
            // TODO: display an error message
        }
    }

    private async void OnNewProjectFromTemplateAsync(object? sender, ProjectTemplatesGrid.ItemActivatedEventArgs args)
    {
        _ = sender;

        var dialog = new NewProjectDialog(args.TemplateInfo) { XamlRoot = this.XamlRoot };

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

                // TODO(abdes) navigate to project workspace for newly created project
            }
        }
    }
}
