// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace Oxygen.Editor.ProjectBrowser.Controls;

/// <summary>
/// A user control that displays a list of recent projects with sorting options by name or last used date.
/// This control uses MVVM pattern with <see cref="RecentProjectsListViewModel"/>.
/// </summary>
internal sealed partial class RecentProjectsList : UserControl
{
    private RecentProjectsListViewModel? vm;

    /// <summary>
    /// Initializes a new instance of the <see cref="RecentProjectsList"/> class.
    /// </summary>
    public RecentProjectsList()
    {
        this.InitializeComponent();
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
        get => this.vm;
        set => this.OnViewModelChanged(value);
    }

    private void OnViewModelChanged(RecentProjectsListViewModel? viewModel)
    {
        this.vm?.ItemActivated -= this.OnViewModelItemActivated;
        this.vm = viewModel;
        this.vm?.ItemActivated += this.OnViewModelItemActivated;
    }

    // IsBusy -> IsListEnabled is now bound; no code-behind required.

    /// <summary>
    /// Handles the double-tap event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (sender is not ListView { SelectedItem: ProjectItemWithThumbnail item })
        {
            return;
        }

        this.ViewModel?.ActivateProjectCommand.Execute(item);
    }

    /// <summary>
    /// Handles the click event on a project item.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void OnProjectItemClicked(object sender, ItemClickEventArgs args)
    {
        if (args.ClickedItem is not ProjectItemWithThumbnail item)
        {
            return;
        }

        this.ViewModel?.ActivateProjectCommand.Execute(item);
    }

    private void OnListViewKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Key is not (VirtualKey.Enter or VirtualKey.Space))
        {
            return;
        }

        if (sender is not ListView { SelectedItem: ProjectItemWithThumbnail { } item })
        {
            return;
        }

        this.ViewModel?.ActivateProjectCommand.Execute(item);
    }

    private void OnViewModelItemActivated(object? sender, RecentProjectActivatedEventArgs args)
        => this.ItemActivated?.Invoke(this, new ProjectItemActivatedEventArgs(args.Item.ProjectInfo));
}
