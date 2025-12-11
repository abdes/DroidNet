// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Model;
using DroidNet.Mvvm.Generators;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.DynamicTree;

/// <summary>
/// A View that shows a hierarchical layout of a <see cref="Project">project</see> that has <see cref="Scene">scenes</see>, which
/// in turn can hold multiple <see cref="Entity">entities</see>. It demonstrates the flexibility of the <see cref="DynamicTree" />
/// in representing hierarchical layouts of mixed types which can be loaded dynamically.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
public sealed partial class ProjectLayoutView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectLayoutView"/> class.
    /// </summary>
    public ProjectLayoutView()
    {
        this.InitializeComponent();
        this.Unloaded += ProjectLayoutView_Unloaded;
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

    private async void ViewModel_RenameRequested(object? sender, ITreeItem? item)
    {
        _ = sender; // unused
        if (item is null)
        {
            return;
        }

        var dialog = new ContentDialog
        {
            Title = "Rename",
            PrimaryButtonText = "OK",
            CloseButtonText = "Cancel"
        };

        var tb = new TextBox() { Text = item.Label };
        dialog.Content = tb;

        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            var newName = tb.Text?.Trim() ?? string.Empty;
            if (item.ValidateItemName(newName))
            {
                item.Label = newName;
                switch (item)
                {
                    case SceneAdapter sceneAdapter:
                        sceneAdapter.AttachedObject.Name = newName;
                        break;
                    case EntityAdapter entityAdapter:
                        entityAdapter.AttachedObject.Name = newName;
                        break;
                    default:
                        break;
                }
            }
            else
            {
                // Optionally show a message, for demo we ignore invalid names.
            }
        }
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

    private async void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        await this.ViewModel!.RemoveSelectedItemsCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }
}
