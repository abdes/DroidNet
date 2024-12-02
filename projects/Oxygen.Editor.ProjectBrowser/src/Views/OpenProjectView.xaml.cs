// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Mvvm.Generators;
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
    /// <summary>
    /// Initializes a new instance of the <see cref="OpenProjectView"/> class.
    /// </summary>
    public OpenProjectView()
    {
        this.InitializeComponent();
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
            await this.ViewModel!.OpenProjectFileCommand.ExecuteAsync((item as INestedItem)!.ParentPath).ConfigureAwait(false);
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
