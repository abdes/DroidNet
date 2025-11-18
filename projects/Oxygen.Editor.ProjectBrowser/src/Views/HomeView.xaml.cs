// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.ProjectBrowser.Controls;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.Templates;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// Start Home page.
/// </summary>
[ViewModel(typeof(HomeViewModel))]
public sealed partial class HomeView
{
    private readonly IProjectBrowserService projectBrowser;
    private readonly RecentProjectsListViewModel? recentProjectsListViewModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="HomeView"/> class.
    /// </summary>
    /// <param name="projectBrowser">The project browser service.</param>
    public HomeView(IProjectBrowserService projectBrowser)
    {
        this.projectBrowser = projectBrowser;
        this.InitializeComponent();

        // Setup the RecentProjectsList control with its ViewModel
        var defaultThumbnail = $"ms-appx:///{typeof(ProjectInfo).Assembly.GetName().Name}/Data/Images/DefaultProjectIcon.png";
        this.recentProjectsListViewModel = new RecentProjectsListViewModel(defaultThumbnail);
        this.RecentProjectsListControl.ViewModel = this.recentProjectsListViewModel;

        Debug.WriteLine("[HomeView] RecentProjectsList ViewModel initialized");

        // Wire up the HomeViewModel's DataContextChanged to sync RecentProjects
        this.Loaded += this.OnHomeViewLoaded;
    }

    /// <summary>
    /// Handles the Loaded event to wire up the HomeViewModel's RecentProjects to the RecentProjectsListViewModel.
    /// </summary>
    private void OnHomeViewLoaded(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        Debug.WriteLine("[HomeView] OnHomeViewLoaded called");
        var viewModel = this.ViewModel;
        if (viewModel is not null && this.recentProjectsListViewModel is not null)
        {
            Debug.WriteLine($"[HomeView] Wiring RecentProjects collection: Current Count={viewModel.RecentProjects.Count}");

            // Set the HomeViewModel's RecentProjects collection directly.
            // The RecentProjectsListViewModel will transform and display items as they're added/removed.
            this.recentProjectsListViewModel.SetRecentProjects(viewModel.RecentProjects);
        }
        else
        {
            Debug.WriteLine($"[HomeView] Cannot wire in OnLoaded: viewModel is null={viewModel is null}, recentProjectsListViewModel is null={this.recentProjectsListViewModel is null}");
        }
    }

    /// <summary>
    /// Handles the event when a recent project is opened.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void OnRecentProjectOpenAsync(object sender, ProjectItemActivatedEventArgs args)
    {
        _ = sender;

        try
        {
            var success = await this.ViewModel!.OpenProjectAsync(args.ProjectInfo).ConfigureAwait(true);
            if (!success)
            {
                Debug.WriteLine($"[HomeView] Failed to open project: {args.ProjectInfo.Name}");

                // Show error and reset activation state
                this.recentProjectsListViewModel?.ResetActivationState();
            }

            // If success, the router will navigate away and close this window, so no need to reset
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            Debug.WriteLine($"[HomeView] Exception opening project: {ex.Message}");
            this.recentProjectsListViewModel?.ResetActivationState();
        }
#pragma warning restore CA1031 // Do not catch general exception types
    }

    /// <summary>
    /// Handles the event when a new project is created from a template.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void OnNewProjectFromTemplateAsync(object? sender, ItemClickEventArgs args)
    {
        _ = sender;

        if (args.ClickedItem is not ITemplateInfo templateInfo)
        {
            return;
        }

        var dialog = new NewProjectDialog(this.projectBrowser, templateInfo) { XamlRoot = this.XamlRoot };

        var result = await dialog.ShowAsync();

        if (result == ContentDialogResult.Primary)
        {
            var success = await this.ViewModel!.NewProjectFromTemplate(
                    templateInfo,
                    dialog.ViewModel.ProjectName,
                    dialog.ViewModel.SelectedLocation.Path)
                .ConfigureAwait(true);

            if (!success)
            {
                // TODO: display an error message
                Debug.WriteLine("Failed to create new project from template");
            }
        }
    }

    private async void OnHomeViewKeyDown(object sender, KeyRoutedEventArgs args)
    {
        _ = sender; // Unused

        if (args.Key == Windows.System.VirtualKey.F5)
        {
            await this.ViewModel!.LoadRecentTemplatesCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await this.ViewModel!.LoadRecentProjectsCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }
}
