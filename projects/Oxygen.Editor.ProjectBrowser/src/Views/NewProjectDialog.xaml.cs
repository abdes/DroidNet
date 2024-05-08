// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// An empty page that can be used on its own or navigated to within a
/// Frame.
/// </summary>
public sealed partial class NewProjectDialog
{
    public NewProjectDialog(ITemplateInfo template)
    {
        this.InitializeComponent();

        this.ViewModel = new NewProjectDialogViewModel(template);
        this.DataContext = this.ViewModel;

        _ = this.ProjectNameTextBox.Focus(FocusState.Programmatic);
    }

    public NewProjectDialogViewModel ViewModel { get; set; }

    private void OnLocationItemClick(object sender, ItemClickEventArgs e)
    {
        this.ViewModel.SetLocationCommand.Execute(e.ClickedItem);
        this.LocationExpander.IsExpanded = false;
    }
}
