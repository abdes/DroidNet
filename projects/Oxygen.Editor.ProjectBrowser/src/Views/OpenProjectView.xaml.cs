// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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

    /// <summary>
    /// Initializes a new instance of the <see cref="OpenProjectView"/> class.
    /// </summary>
    public OpenProjectView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnOpenProjectViewLoaded;
    }

    /// <summary>
    /// Handles the Loaded event to capture the ListView reference and subscribe to IsActivating changes.
    /// </summary>
    private void OnOpenProjectViewLoaded(object sender, RoutedEventArgs e)
    {
        this.filesListView = this.FindName("FilesList") as ListView;
        if (this.ViewModel is not null && this.filesListView is not null)
        {
            this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
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
    /// Updates the ListView enabled state based on IsActivating.
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
        Debug.WriteLine($"Item {item.Location} clicked", StringComparer.Ordinal);

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
}
