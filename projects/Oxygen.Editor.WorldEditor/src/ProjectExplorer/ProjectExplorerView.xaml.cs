// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// A View that shows a hierarchical layout of a <see cref="Project">project</see> that has <see cref="Scene">scenes</see>, which
/// in turn can hold multiple <see cref="GameEntity">entities</see>. It demonstrates the flexibility of the <see cref="DynamicTree" />
/// in representing hierarchical layouts of mixed types which can be loaded dynamically.
/// </summary>
[ViewModel(typeof(ProjectExplorerViewModel))]
public sealed partial class ProjectExplorerView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ProjectExplorerView"/> class.
    /// </summary>
    public ProjectExplorerView()
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

    private async void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        await this.ViewModel!.RemoveSelectedItemsCommand.ExecuteAsync(parameter: null).ConfigureAwait(false);
    }
}
