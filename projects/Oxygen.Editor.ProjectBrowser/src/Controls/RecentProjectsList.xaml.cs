// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A user control that displays a list of recent projects with sorting options by name or last used date.
/// This control uses MVVM pattern with <see cref="RecentProjectsListViewModel"/>.
/// </summary>
public sealed partial class RecentProjectsList : UserControl
{
    private RecentProjectsListViewModel? cachedViewModel;
    private ListView? projectsListView;

    /// <summary>
    /// Initializes a new instance of the <see cref="RecentProjectsList"/> class.
    /// </summary>
    public RecentProjectsList()
    {
        this.InitializeComponent();
        this.Loaded += this.OnRecentProjectsListLoaded;
        Debug.WriteLine("[RecentProjectsList] Control initialized");
    }

    /// <summary>
    /// Occurs when a project item in the list is activated by the user.
    /// </summary>
    public event EventHandler<ProjectItemActivatedEventArgs>? ItemActivated;

    /// <summary>
    /// Gets or sets the ViewModel for this control.
    /// </summary>
    internal RecentProjectsListViewModel? ViewModel
    {
        get => this.cachedViewModel;
        set => this.OnViewModelChanged(value);
    }

    private void OnRecentProjectsListLoaded(object sender, RoutedEventArgs e)
    {
        // Capture the ListView reference for IsEnabled binding
        this.projectsListView = this.FindName("ProjectsListView") as ListView;
        if (this.projectsListView is not null)
        {
            this.UpdateListViewState();
            Debug.WriteLine("[RecentProjectsList] ListView captured");
        }
    }

    /// <summary>
    /// Handles changes to the ViewModel property.
    /// </summary>
    /// <param name="viewModel">The new ViewModel instance.</param>
    private void OnViewModelChanged(RecentProjectsListViewModel? viewModel)
    {
        // Unsubscribe from old ViewModel
        if (this.cachedViewModel is not null)
        {
            this.cachedViewModel.ItemActivated -= this.OnViewModelItemActivated;
            this.cachedViewModel.PropertyChanged -= this.OnViewModelPropertyChanged;
        }

        // Update and subscribe to new ViewModel
        this.cachedViewModel = viewModel;
        if (this.cachedViewModel is not null)
        {
            this.cachedViewModel.ItemActivated += this.OnViewModelItemActivated;
            this.cachedViewModel.PropertyChanged += this.OnViewModelPropertyChanged;

            // Update ListView state initially
            this.UpdateListViewState();
        }
    }

    private void OnViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(RecentProjectsListViewModel.IsActivating), StringComparison.Ordinal))
        {
            this.UpdateListViewState();
        }
    }

    private void UpdateListViewState()
    {
        if (this.projectsListView is not null && this.ViewModel is not null)
        {
            this.projectsListView.IsEnabled = !this.ViewModel.IsActivating;
        }
    }

    /// <summary>
    /// Handles the double-tap event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        _ = args; // unused
        Debug.WriteLine("[RecentProjectsList] OnProjectItemDoubleTapped called");

        var listView = (ListView)sender;
        var selectedItem = (ProjectItemWithThumbnail?)listView.SelectedItem;
        if (selectedItem is null)
        {
            Debug.WriteLine("[RecentProjectsList] OnProjectItemDoubleTapped: No selected item");
            return;
        }

        var vm = this.ViewModel;
        if (vm?.ActivateProjectCommand is not null)
        {
            Debug.WriteLine($"[RecentProjectsList] OnProjectItemDoubleTapped: Executing ActivateProjectCommand for {selectedItem.ProjectInfo.Name}");
            vm.ActivateProjectCommand.Execute(selectedItem);
        }
    }

    /// <summary>
    /// Handles the click event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemClicked(object sender, ItemClickEventArgs args)
    {
        Debug.WriteLine("[RecentProjectsList] OnProjectItemClicked called");
        var item = (ProjectItemWithThumbnail?)args.ClickedItem;
        if (item is null)
        {
            Debug.WriteLine("[RecentProjectsList] OnProjectItemClicked: No clicked item");
            return;
        }

        var vm = this.ViewModel;
        if (vm?.ActivateProjectCommand is not null)
        {
            Debug.WriteLine($"[RecentProjectsList] OnProjectItemClicked: Executing ActivateProjectCommand for {item.ProjectInfo.Name}");
            vm.ActivateProjectCommand.Execute(item);
        }
    }

    /// <summary>
    /// Handles the key down event on the list view.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="eventArgs">The event data.</param>
    private void OnListViewKeyDown(object sender, KeyRoutedEventArgs eventArgs)
    {
        Debug.WriteLine($"[RecentProjectsList] OnListViewKeyDown: Key={eventArgs.Key}");
        if (eventArgs.Key is VirtualKey.Enter or VirtualKey.Space)
        {
            var listView = (ListView)sender;
            var selectedItem = (ProjectItemWithThumbnail?)listView.SelectedItem;
            if (selectedItem is null)
            {
                Debug.WriteLine("[RecentProjectsList] OnListViewKeyDown: No selected item");
                return;
            }

            var vm = this.ViewModel;
            if (vm?.ActivateProjectCommand is not null)
            {
                Debug.WriteLine($"[RecentProjectsList] OnListViewKeyDown: Executing ActivateProjectCommand for {selectedItem.ProjectInfo.Name}");
                vm.ActivateProjectCommand.Execute(selectedItem);
            }
        }
    }

    /// <summary>
    /// Handles the ItemActivated event from the ViewModel.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnViewModelItemActivated(object? sender, RecentProjectActivatedEventArgs args)
    {
        Debug.WriteLine($"[RecentProjectsList] OnViewModelItemActivated: Project={args.Item.ProjectInfo.Name}");
        this.ItemActivated?.Invoke(this, new ProjectItemActivatedEventArgs(args.Item.ProjectInfo));
    }
}
