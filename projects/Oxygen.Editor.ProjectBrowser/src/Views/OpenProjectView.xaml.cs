// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
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
    /// <summary>
    /// Initializes a new instance of the <see cref="OpenProjectView"/> class.
    /// </summary>
    public OpenProjectView()
    {
        this.InitializeComponent();
    }

    /// <summary>
    /// Handles the click event on a known location button.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="e">The known location that was clicked.</param>
    private void KnownLocationButton_OnClick(object? sender, KnownLocation e)
    {
        _ = sender;

        this.ViewModel!.SelectLocation(e);
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
            this.ViewModel!.ChangeFolder(item.Location);
        }
        else if (item.Name.EndsWith(".oxy", ignoreCase: true, CultureInfo.InvariantCulture))
        {
            _ = await this.ViewModel!.OpenProjectFile((item as INestedItem)!.ParentPath).ConfigureAwait(false);
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

        this.ViewModel!.ApplyFilter(this.FilterBox.Text);
    }

    /// <summary>
    /// Handles the loaded event of the page.
    /// </summary>
    /// <param name="sender">The source of the event.</param>
    /// <param name="args">The event data.</param>
    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        await this.ViewModel!.Initialize().ConfigureAwait(true);
    }
}
