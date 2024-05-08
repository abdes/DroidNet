// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// An empty page that can be used on its own or navigated to within a
/// Frame.
/// </summary>
public sealed partial class StartNewPage
{
    /// <summary>Initializes a new instance of the <see cref="StartNewPage" /> class.</summary>
    public StartNewPage()
    {
        this.InitializeComponent();

        this.ViewModel = Ioc.Default.GetRequiredService<StartNewViewModel>();
    }

    public StartNewViewModel ViewModel { get; }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        Debug.WriteLine("Navigation to StartNewPage.");

        this.ViewModel.LoadTemplates();
    }

    private void OnTemplateClicked(object? sender, ITemplateInfo item)
        => this.ViewModel.SelectItem(item);

    private async void CreateButton_OnClick(object? sender, RoutedEventArgs e)
    {
        var template = this.ViewModel.SelectedItem!;
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
