// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;
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
        };

        var tb = new TextBox() { Text = item.Label };
        dialog.Content = tb;

        if (this.XamlRoot is not null)
        {
            dialog.XamlRoot = this.XamlRoot;
        }

        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            var newName = tb.Text?.Trim() ?? string.Empty;

            // Basic validation could go here
            await this.ViewModel!.RenameItemAsync(item, newName).ConfigureAwait(false);
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

    // UI event handlers for toolbar buttons added in XAML. These will try to execute
    // corresponding ViewModel commands if present, otherwise log a message.
    private void NewFolderFromSelection_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!this.TryExecuteViewModelCommand("CreateFolderCommand") &&
            !this.TryExecuteViewModelMethod("CreateFolder"))
        {
            Debug.WriteLine("NewFolderFromSelection clicked - not implemented in ViewModel.");
        }
    }

    private void Cut_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!this.TryExecuteViewModelCommand("CutCommand") && !this.TryExecuteViewModelMethod("Cut"))
        {
            Debug.WriteLine("Cut clicked - not implemented in ViewModel.");
        }
    }

    private void Copy_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!this.TryExecuteViewModelCommand("CopyCommand") && !this.TryExecuteViewModelMethod("Copy"))
        {
            Debug.WriteLine("Copy clicked - not implemented in ViewModel.");
        }
    }

    private void Paste_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!this.TryExecuteViewModelCommand("PasteCommand") && !this.TryExecuteViewModelMethod("Paste"))
        {
            Debug.WriteLine("Paste clicked - not implemented in ViewModel.");
        }
    }

    private void Rename_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!this.TryExecuteViewModelCommand("RenameSelectedCommand"))
        {
            Debug.WriteLine("Rename clicked - not implemented in ViewModel.");
        }
    }

    private bool TryExecuteViewModelCommand(string commandPropertyName)
    {
        try
        {
            var vm = this.ViewModel;
            if (vm is null)
            {
                return false;
            }

            var prop = vm.GetType().GetProperty(commandPropertyName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (prop is null)
            {
                return false;
            }

            if (prop.GetValue(vm) is System.Windows.Input.ICommand cmd && cmd.CanExecute(null))
            {
                cmd.Execute(null);
                return true;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error executing ViewModel command '{commandPropertyName}': {ex}");
        }

        return false;
    }

    private bool TryExecuteViewModelMethod(string methodName)
    {
        try
        {
            var vm = this.ViewModel;
            if (vm is null)
            {
                return false;
            }

            var method = vm.GetType().GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (method is null)
            {
                return false;
            }

            var parameters = method.GetParameters();
            if (parameters.Length == 0)
            {
                var result = method.Invoke(vm, null);
                return true;
            }

            // No support for parameterized methods in this helper
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error invoking ViewModel method '{methodName}': {ex}");
        }

        return false;
    }
}
