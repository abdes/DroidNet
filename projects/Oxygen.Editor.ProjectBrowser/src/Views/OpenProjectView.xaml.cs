// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.ProjectBrowser.Projects;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Storage;
using WinRT;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// A page that can be used on its own or navigated to within a Frame to open projects.
/// </summary>
[ViewModel(typeof(OpenProjectViewModel))]
public sealed partial class OpenProjectView
{
    private ListView? filesListView;
    private ScrollViewer? headerScrollViewer;

    /// <summary>
    /// Initializes a new instance of the <see cref="OpenProjectView"/> class.
    /// </summary>
    public OpenProjectView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnOpenProjectViewLoaded;
        this.Unloaded += this.OnOpenProjectViewUnloaded;
        this.DataContextChanged += this.OnDataContextChanged;
    }

    /// <summary>
    /// Finds a descendant element by name and returns it cast to the specified type.
    /// </summary>
    private static T? FindDescendantByName<T>(DependencyObject parent, string name)
        where T : DependencyObject
    {
        if (parent is null)
        {
            return null;
        }

        for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is FrameworkElement fe && string.Equals(fe.Name, name, StringComparison.Ordinal) && child is T found)
            {
                return found;
            }

            var result = FindDescendantByName<T>(child, name);
            if (result is not null)
            {
                return result;
            }
        }

        return null;
    }

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        _ = sender;

        // Ensure the proxy is kept in sync if the ViewModel is created/changed after Loaded
        if (this.Resources.TryGetValue("ViewModelProxy", out var proxyObj) && proxyObj is Oxygen.Editor.ProjectBrowser.Controls.BindingProxy proxy)
        {
            proxy.Data = this.ViewModel;
        }
    }

    /// <summary>
    /// Handles the Loaded event to capture the ListView reference and subscribe to IsBusy changes.
    /// </summary>
    private void OnOpenProjectViewLoaded(object sender, RoutedEventArgs e)
    {
        this.filesListView = this.FindName("FilesList") as ListView;
        this.headerScrollViewer = this.FindName("HeaderScrollViewer") as ScrollViewer;

        if (this.ViewModel is not null && this.filesListView is not null)
        {
            this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;

            // Initialize binding proxy to expose the ViewModel to item templates
            if (this.Resources.TryGetValue("ViewModelProxy", out var proxyObj) && proxyObj is Oxygen.Editor.ProjectBrowser.Controls.BindingProxy proxy)
            {
                proxy.Data = this.ViewModel;
            }

            // Sync scroll viewers
            var listScrollViewer = FindDescendantByName<ScrollViewer>(this.filesListView, "ScrollViewer");
            if (listScrollViewer != null)
            {
                listScrollViewer.ViewChanged += this.ListScrollViewer_ViewChanged;
            }
        }
    }

    private void ListScrollViewer_ViewChanged(object? sender, ScrollViewerViewChangedEventArgs e)
    {
        if (sender is ScrollViewer listScrollViewer && this.headerScrollViewer is not null)
        {
            // Sync the header scroll viewer to match the list's horizontal offset
            this.headerScrollViewer.ChangeView(listScrollViewer.HorizontalOffset, null, null, true);
        }
    }

    /// <summary>
    /// Handles property changes on the ViewModel to update ListView state.
    /// </summary>
    private void OnViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(OpenProjectViewModel.IsActivating), StringComparison.Ordinal))
        {
            this.UpdateListViewState();
        }
    }

    /// <summary>
    /// Updates the ListView enabled state based on IsBusy.
    /// </summary>
    private void UpdateListViewState()
    {
        if (this.filesListView is not null && this.ViewModel is not null)
        {
            this.filesListView.IsEnabled = !this.ViewModel.IsActivating;
        }
    }

    /// <summary>
    /// Handles the item click event in the list view asynchronously.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The event data.</param>
    private async void ListView_OnItemClickAsync(object sender, ItemClickEventArgs e)
    {
        _ = sender;

        var item = e.ClickedItem.As<IStorageItem>();

        if (item is IFolder)
        {
            this.ViewModel!.ChangeFolderCommand.Execute(item.Location);
        }
        else if (item.Name.EndsWith(".oxy", ignoreCase: true, CultureInfo.InvariantCulture))
        {
            try
            {
                await this.ViewModel!.OpenProjectFileCommand.ExecuteAsync((item as INestedItem)!.ParentPath).ConfigureAwait(true);
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine($"[OpenProjectView] Opening project file was cancelled");
                this.ViewModel?.ResetActivationState();
            }
        }
    }

    /// <summary>
    /// Handles the text changed event in the filter box.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private void FilterBox_OnTextChanged(object sender, TextChangedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.ViewModel!.ApplyFilterCommand.Execute(this.FilterBox.Text);
    }

    private void OnOpenProjectViewUnloaded(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.SaveColumnWidths();
            this.ViewModel.PropertyChanged -= this.OnViewModelPropertyChanged;
        }
    }

    private void OnKnownLocationItemClick(object sender, ItemClickEventArgs e)
    {
        _ = sender;
        if (e.ClickedItem is KnownLocation location)
        {
            this.ViewModel?.SelectLocationCommand.Execute(location);
        }
    }

    private void OnKnownLocationSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _ = sender;
        if (e.AddedItems.Count > 0 && e.AddedItems[0] is KnownLocation location)
        {
            this.ViewModel?.SelectLocationCommand.Execute(location);
        }
    }
}
