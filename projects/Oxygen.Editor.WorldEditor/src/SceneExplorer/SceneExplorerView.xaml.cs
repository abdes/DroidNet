// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     A View that shows a hierarchical layout of a <see cref="Scene">scene</see>, which
///     in turn can hold multiple <see cref="SceneNode">entities</see>.
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

        this.ViewModel!.UndoCommand.Execute(null);
    }

    private void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        this.ViewModel!.RedoCommand.Execute(null);
    }

    private void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused
        args.Handled = true;

        this.ViewModel!.RemoveSelectedItemsCommand.Execute(null);
    }
}
