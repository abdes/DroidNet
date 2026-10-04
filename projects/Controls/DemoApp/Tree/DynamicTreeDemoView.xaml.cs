// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// A Demo view that shows one opened <see cref="Scene"/> as the tree root with its <see cref="Entity"/> hierarchy.
/// </summary>
[ViewModel(typeof(DynamicTreeDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views must be public")]
public sealed partial class DynamicTreeDemoView
{
    private ResourceDictionary? compactFilterStyles;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicTreeDemoView"/> class.
    /// </summary>
    public DynamicTreeDemoView()
    {
        this.InitializeComponent();
        this.Unloaded += this.DynamicTreeDemoView_Unloaded;
    }

    private async void DynamicTreeDemoView_OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ViewModel is not null)
        {
            await this.ViewModel.LoadOpenedSceneCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            this.ViewModel.RenameRequested += this.ViewModel_RenameRequested;
            this.SceneTree.ItemContextRequested += this.SceneTree_ItemContextRequested;
        }
    }

    private void DynamicTreeDemoView_Unloaded(object? sender, RoutedEventArgs e)
    {
        if (this.ViewModel is { } viewModel)
        {
            viewModel.RenameRequested -= this.ViewModel_RenameRequested;
            this.SceneTree.ItemContextRequested -= this.SceneTree_ItemContextRequested;
        }
    }

    private async void ViewModel_RenameRequested(object? sender, RenameRequestedEventArgs? args)
    {
        _ = sender; // unused
        var item = args?.Item;
        if (item is null)
        {
            return;
        }

        _ = await this.SceneTree.BeginRenameAsync(item).ConfigureAwait(true);
    }

    private void SceneTree_ItemContextRequested(object? sender, TreeItemContextRequestedEventArgs args)
    {
        _ = sender;
        var menu = new MenuFlyout();
        var select = new MenuFlyoutItem { Text = $"Focus {args.Item.Label}" };
        select.Click += (_, _) => this.ViewModel?.SelectItemCommand.Execute(
            new ItemSelectionArgs(args.Item, DynamicTreeViewModel.RequestOrigin.Programmatic, IsCtrlKeyDown: false, IsShiftKeyDown: false));
        menu.Items.Add(select);

        if (args.Position is { } position)
        {
            menu.ShowAt(args.Anchor, position);
        }
        else
        {
            menu.ShowAt(args.Anchor);
        }
    }

    private void EntityLockButton_OnClick(object sender, RoutedEventArgs args)
    {
        _ = args;
        if (sender is Button { DataContext: ITreeItem item })
        {
            item.IsLocked = !item.IsLocked;
        }
    }

    private void TreeDensityToggle_OnClick(object sender, RoutedEventArgs args)
    {
        _ = args;
        if (sender is not ToggleButton densityToggle || this.SceneTree is not { } sceneTree)
        {
            return;
        }

        this.UpdateTreePresentation();
    }

    private void LayoutRootGrid_OnSizeChanged(object sender, SizeChangedEventArgs args)
    {
        _ = sender;
        var isNarrow = args.NewSize.Width < 760;
        this.TreeToolbar.IsCompact = isNarrow;
        this.ToolbarSpacerColumn.Width = new GridLength(isNarrow ? 1 : 3, GridUnitType.Star);
        this.ToolbarItemsColumn.Width = new GridLength(isNarrow ? 4 : 2, GridUnitType.Star);
        this.HistoryPanel.Visibility = isNarrow ? Visibility.Collapsed : Visibility.Visible;
        this.TreePanelColumn.Width = new GridLength(isNarrow ? 1 : 4, GridUnitType.Star);
        this.HistoryPanelColumn.Width = isNarrow ? new GridLength(0) : new GridLength(1, GridUnitType.Star);
        this.UpdateTreePresentation();
    }

    private void UpdateTreePresentation()
    {
        if (this.SceneTree is { } sceneTree)
        {
            var isNarrow = this.LayoutRootGrid.ActualWidth < 760;
            var isCompact = this.TreeDensityToggle.IsChecked == true;
            sceneTree.ItemRowHeight = isCompact ? 28 : 40;
            var compactFilterStyles = this.compactFilterStyles ??= new ResourceDictionary
            {
                Source = new Uri("ms-appx:///DroidNet.Controls.DynamicTree/DynamicTree/CompactFilterBarStyles.xaml"),
            };
            if (isCompact && !sceneTree.Resources.MergedDictionaries.Contains(compactFilterStyles))
            {
                sceneTree.Resources.MergedDictionaries.Add(compactFilterStyles);
            }
            else if (!isCompact)
            {
                _ = sceneTree.Resources.MergedDictionaries.Remove(compactFilterStyles);
            }

            var filterStyles = isCompact ? compactFilterStyles : this.Resources;
            sceneTree.FilterBarInputStyle = (Style)filterStyles["FilterBarInputStyle"];
            this.FilterMenuButton.Style = (Style)filterStyles["FilterBarButtonStyle"];
            sceneTree.ItemFontSize = isCompact ? 12 : 14;
            sceneTree.ItemIconSize = isCompact ? 18 : 24;
            var iconMargin = isNarrow ? (isCompact ? 1 : 2) : (isCompact ? 2 : 4);
            sceneTree.ItemIconMargin = new Thickness(iconMargin, 0, iconMargin, 0);
            sceneTree.ItemIndentWidth = isNarrow
                ? isCompact ? 18 : 22
                : isCompact ? 28 : 34;
        }
    }

    private async void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        await this.ViewModel!.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }

    private async void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        await this.ViewModel!.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }

    private async void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        await this.ViewModel!.RemoveSelectedItemsCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }
}
