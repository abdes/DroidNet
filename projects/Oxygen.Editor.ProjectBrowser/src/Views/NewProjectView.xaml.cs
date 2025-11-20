// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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
    private Control? templatesGridControl;

    /// <summary>
    /// Initializes a new instance of the <see cref="NewProjectView"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    public NewProjectView(IProjectBrowserService projectBrowser)
    {
        this.InitializeComponent();

        this.projectBrowser = projectBrowser;
        this.Loaded += this.OnNewProjectViewLoaded;
    }

    /// <summary>
    /// Handles the Loaded event to capture the grid reference and subscribe to IsBusy changes.
    /// </summary>
    private void OnNewProjectViewLoaded(object sender, RoutedEventArgs e)
    {
        this.templatesGridControl = this.FindName("TemplatesGridControl") as Control;
        if (this.ViewModel is not null && this.templatesGridControl is not null)
        {
            this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
        }
    }

    /// <summary>
    /// Handles property changes on the ViewModel to update grid state.
    /// </summary>
    private void OnViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(NewProjectViewModel.IsActivating), StringComparison.Ordinal))
        {
            this.UpdateControlState();
        }
    }

    /// <summary>
    /// Updates the control enabled state based on IsBusy.
    /// </summary>
    private void UpdateControlState()
    {
        if (this.templatesGridControl is not null && this.ViewModel is not null)
        {
            this.templatesGridControl.IsEnabled = !this.ViewModel.IsActivating;
        }
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
            dialog.ViewModel.IsActivating = true;
            var success = await this.ViewModel.NewProjectFromTemplate(
                    template,
                    dialog.ViewModel.ProjectName,
                    dialog.ViewModel.SelectedLocation.Path)
                .ConfigureAwait(true);

            if (!success)
            {
                // TODO: display an error message
                dialog.ViewModel.ResetActivationState();
            }
        }
    }
}
