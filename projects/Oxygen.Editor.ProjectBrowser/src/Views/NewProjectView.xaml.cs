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
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// An empty page that can be used on its own or navigated to within a
/// Frame.
/// </summary>
[ViewModel(typeof(NewProjectViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class NewProjectView
{
    /// <summary>Initializes a new instance of the <see cref="NewProjectView" /> class.</summary>
    public NewProjectView() => this.InitializeComponent();

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ViewModel!.LoadTemplates();
    }

    private void OnTemplateClicked(object? sender, ITemplateInfo item)
    {
        _ = sender;

        this.ViewModel!.SelectItem(item);
    }

    private async void CreateButton_OnClick(object? sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        var template = this.ViewModel!.SelectedItem!;
        var dialog = new NewProjectDialog(template) { XamlRoot = this.XamlRoot };

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

                // TODO(abdes) navigate to project workspace for newly created project
            }
        }
    }
}
