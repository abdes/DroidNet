// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
///     A View that shows a hierarchical layout of a <see cref="World.Scene">scene</see>, which
///     in turn can hold multiple <see cref="World.SceneNode">entities</see>.
/// </summary>
[ViewModel(typeof(SceneExplorerViewModel))]
public sealed partial class SceneExplorerView
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneExplorerView" /> class.
    /// </summary>
    public SceneExplorerView()
    {
        this.InitializeComponent();
        this.Loaded += this.SceneExplorerView_Loaded;
        this.Unloaded += this.SceneExplorerView_Unloaded;
    }

    private void SceneExplorerView_Loaded(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel is not null)
        {
            this.ViewModel.RenameRequested += this.ViewModel_RenameRequested;
        }
    }

    private void SceneExplorerView_Unloaded(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel is not null)
        {
            this.ViewModel.RenameRequested -= this.ViewModel_RenameRequested;
        }
    }

    private async void ViewModel_RenameRequested(object? sender, RenameRequestedEventArgs? args)
    {
        var item = args?.Item;
        if (item is null)
        {
            return;
        }

        var dialog = new ContentDialog
        {
            Title = "Rename",
            PrimaryButtonText = "OK",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        };

        var tb = new TextBox() { Text = item.Label };
        dialog.Content = tb;

        // Pressing Enter commits the draft and closes the dialog, matching the OK button.
        var confirmed = false;
        tb.KeyDown += (_, e) =>
        {
            if (e.Key == Windows.System.VirtualKey.Enter)
            {
                e.Handled = true;
                confirmed = true;
                dialog.Hide();
            }
        };

        if (this.XamlRoot is not null)
        {
            dialog.XamlRoot = this.XamlRoot;
        }

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary && !confirmed)
        {
            return;
        }

        var newName = tb.Text?.Trim() ?? string.Empty;
        if (string.Equals(newName, item.Label, StringComparison.Ordinal))
        {
            return;
        }

        _ = await this.ViewModel!.CommitRenameAsync(item, newName).ConfigureAwait(false);
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

    // UI event handlers for toolbar buttons added in XAML. These invoke the typed
    // ViewModel commands directly rather than through reflection.
    private void NewFolderFromSelection_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        this.ViewModel?.CreateFolderCommand.Execute(null);
    }

    private void Cut_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        this.ViewModel?.CutCommand.Execute(null);
    }

    private void Copy_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        this.ViewModel?.CopyCommand.Execute(null);
    }

    private void Paste_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        this.ViewModel?.PasteCommand.Execute(null);
    }

    private void Rename_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        this.ViewModel?.RenameSelectedCommand.Execute(null);
    }
}
