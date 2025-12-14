// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reflection;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

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
    }

    private void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        this.ViewModel!.UndoCommand.Execute(parameter: null);
    }

    private void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        this.ViewModel!.RedoCommand.Execute(parameter: null);
    }

    private void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        this.ViewModel!.RemoveSelectedItemsCommand.Execute(parameter: null);
    }

    // UI event handlers for toolbar buttons added in XAML. These will try to execute
    // corresponding ViewModel commands if present, otherwise log a message.
    private void NewFolderFromSelection_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!TryExecuteViewModelCommand("CreateFolderFromSelectionCommand") &&
            !TryExecuteViewModelMethod("CreateFolderFromSelection"))
        {
            Debug.WriteLine("NewFolderFromSelection clicked - not implemented in ViewModel.");
        }
    }

    private void Cut_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!TryExecuteViewModelCommand("CutCommand") && !TryExecuteViewModelMethod("Cut"))
        {
            Debug.WriteLine("Cut clicked - not implemented in ViewModel.");
        }
    }

    private void Copy_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!TryExecuteViewModelCommand("CopyCommand") && !TryExecuteViewModelMethod("Copy"))
        {
            Debug.WriteLine("Copy clicked - not implemented in ViewModel.");
        }
    }

    private void Paste_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!TryExecuteViewModelCommand("PasteCommand") && !TryExecuteViewModelMethod("Paste"))
        {
            Debug.WriteLine("Paste clicked - not implemented in ViewModel.");
        }
    }

    private void Rename_Click(object? sender, RoutedEventArgs e)
    {
        _ = sender;
        if (!TryExecuteViewModelCommand("RenameCommand") && !TryExecuteViewModelMethod("RenameSelected"))
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
                return false;

            var prop = vm.GetType().GetProperty(commandPropertyName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (prop is null)
                return false;

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
                return false;

            var method = vm.GetType().GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (method is null)
                return false;

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
