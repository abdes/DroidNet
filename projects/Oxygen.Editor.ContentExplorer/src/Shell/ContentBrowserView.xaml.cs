// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>
/// The View for the Content Browser UI.
/// </summary>
[ViewModel(typeof(ContentBrowserViewModel))]
public sealed partial class ContentBrowserView : UserControl
{
    private const double DefaultDetailsPaneWidth = 300;

    /// <summary>
    /// Initializes a new instance of the <see cref="ContentBrowserView"/> class.
    /// </summary>
    public ContentBrowserView()
    {
        this.InitializeComponent();
        this.Loaded += (_, _) =>
        {
            this.ViewModelChanged += this.OnViewModelChanged;
            this.ViewModel?.Presentation.PropertyChanged += this.OnPresentationChanged;
            this.SyncViewButtons();
        };
        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.OnViewModelChanged;
            this.ViewModel?.Presentation.PropertyChanged -= this.OnPresentationChanged;
        };
    }

    /// <summary>Gives the details pane its width when shown and none when hidden.</summary>
    /// <param name="isOpen">Whether the details pane is shown.</param>
    /// <returns>The details column width.</returns>
    internal static GridLength DetailsPaneWidth(bool isOpen) => new(isOpen ? DefaultDetailsPaneWidth : 0);

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<ContentBrowserViewModel> args)
    {
        args.OldValue?.Presentation.PropertyChanged -= this.OnPresentationChanged;
        this.ViewModel?.Presentation.PropertyChanged += this.OnPresentationChanged;
        this.SyncViewButtons();
    }

    private void OnPresentationChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(AssetBrowserPresentation.View), StringComparison.Ordinal))
        {
            this.SyncViewButtons();
        }
    }

    // The three view buttons behave as one choice: the current view stays checked when clicked again.
    private async void OnViewButtonClicked(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is not { } model || sender is not ToggleButton { Tag: string view })
        {
            return;
        }

        this.SyncViewButtons();
        var command = view switch
        {
            "List" => model.SwitchToListViewCommand,
            "Details" => model.SwitchToDetailsViewCommand,
            _ => model.SwitchToTilesViewCommand,
        };
        await command.ExecuteAsync(parameter: null).ConfigureAwait(true);
        this.SyncViewButtons();
    }

    private void SyncViewButtons()
    {
        var view = this.ViewModel?.Presentation.View;
        this.TilesViewButton.IsChecked = view == AssetBrowserView.Tiles;
        this.ListViewButton.IsChecked = view == AssetBrowserView.List;
        this.DetailsViewButton.IsChecked = view == AssetBrowserView.Details;
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
