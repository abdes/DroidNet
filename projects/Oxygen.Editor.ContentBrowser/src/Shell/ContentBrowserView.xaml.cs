// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>
/// The View for the Content Browser UI.
/// </summary>
[ViewModel(typeof(ContentBrowserViewModel))]
public sealed partial class ContentBrowserView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ContentBrowserView"/> class.
    /// </summary>
    public ContentBrowserView()
    {
        this.InitializeComponent();
    }

    private async void BreadcrumbBar_ItemClicked(BreadcrumbBar sender, BreadcrumbBarItemClickedEventArgs args)
    {
        // Get index of clicked item from the BreadcrumbBar
        var items = sender.ItemsSource as System.Collections.IList;
        var index = items?.IndexOf(args.Item) ?? -1;
        if (index >= 0 && this.ViewModel is ContentBrowserViewModel vm)
        {
            await vm.NavigateToBreadcrumbAsync(index).ConfigureAwait(false);
        }
    }
}
