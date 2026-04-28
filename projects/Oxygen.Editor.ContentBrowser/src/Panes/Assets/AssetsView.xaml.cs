// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser;

/// <summary>
/// Represents the view for displaying assets in the World Editor.
/// </summary>
[ViewModel(typeof(AssetsViewModel))]
public sealed partial class AssetsView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetsView"/> class.
    /// </summary>
    public AssetsView()
    {
        this.InitializeComponent();
    }

    private async void OnCreateMaterialClicked(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        var defaultFolder = this.ViewModel.GetSelectedMaterialFolder();
        var nameBox = new TextBox
        {
            Header = "Name",
            Text = this.ViewModel.CreateDefaultMaterialName(defaultFolder),
        };
        var folderBox = new TextBox
        {
            Header = "Folder",
            Text = defaultFolder,
        };
        var panel = new StackPanel { Spacing = 12 };
        panel.Children.Add(nameBox);
        panel.Children.Add(folderBox);

        var dialog = new ContentDialog
        {
            Title = "New Material",
            Content = panel,
            PrimaryButtonText = "Create",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = this.XamlRoot,
        };

        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            await this.ViewModel.CreateNewMaterialAsync(nameBox.Text, folderBox.Text).ConfigureAwait(true);
        }
    }
}
