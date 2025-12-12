// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// A View that shows a hierarchical layout of a <see cref="Project">project</see> that has <see cref="Scene">scenes</see>, which
/// in turn can hold multiple <see cref="Entity">entities</see>. It demonstrates the flexibility of the <see cref="DynamicTree" />
/// in representing hierarchical layouts of mixed types which can be loaded dynamically.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views must be public")]
public sealed partial class ProjectLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectLayoutView"/> class.
    /// </summary>
    public ProjectLayoutView()
    {
        this.InitializeComponent();
        this.Unloaded += this.ProjectLayoutView_Unloaded;
    }

    private async void ProjectLayoutView_OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ViewModel is not null)
        {
            await this.ViewModel.LoadProjectCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            this.ViewModel.RenameRequested += this.ViewModel_RenameRequested;
        }
    }

    private void ProjectLayoutView_Unloaded(object? sender, RoutedEventArgs e)
    {
        if (this.ViewModel is not null)
        {
            this.ViewModel.RenameRequested -= this.ViewModel_RenameRequested;
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

        var dialog = new ContentDialog
        {
            Title = "Rename",
            PrimaryButtonText = "OK",
            CloseButtonText = "Cancel",
        };

        var tb = new TextBox() { Text = item.Label };
        dialog.Content = tb;

        // Ensure the dialog has a XamlRoot so ShowAsync() can display it.
        // When a control raises the request from a non-visual context, the
        // ContentDialog needs an explicit XamlRoot set to avoid
        // "This element does not have a XamlRoot" exceptions.
        if (this.XamlRoot is not null)
        {
            dialog.XamlRoot = this.XamlRoot;
        }

        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            var newName = tb.Text?.Trim() ?? string.Empty;
            if (item.ValidateItemName(newName))
            {
                this.ViewModel!.RenameItem(item, newName);
            }
            else
            {
                // Optionally show a message, for demo we ignore invalid names.
            }
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
