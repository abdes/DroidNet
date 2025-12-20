// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Represents the view for editing a scene node in the properties editor.
/// </summary>
[ViewModel(typeof(SceneNodeEditorViewModel))]
public sealed partial class SceneNodeEditorView : UserControl
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNodeEditorView" /> class.
    /// </summary>
    public SceneNodeEditorView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, _) =>
        {
            if (this.ViewModel is null)
            {
                return;
            }

            this.Resources["VmToViewConverter"] = this.ViewModel.VmToViewConverter;
        };
    }

    private async void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused

        var vm = this.ViewModel;
        if (vm is null)
        {
            return;
        }

        var historyKeeper = vm.History;
        if (historyKeeper.UndoStack.Count == 0)
        {
            return;
        }

        args.Handled = true;
        await historyKeeper.UndoAsync().ConfigureAwait(false);
    }

    private async void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused

        var vm = this.ViewModel;
        if (vm is null)
        {
            return;
        }

        var historyKeeper = vm.History;
        if (historyKeeper.RedoStack.Count == 0)
        {
            return;
        }

        args.Handled = true;
        await historyKeeper.RedoAsync().ConfigureAwait(false);
    }
}
