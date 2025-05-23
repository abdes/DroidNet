// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Model;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

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
    }

    private async void ProjectLayoutView_OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.ViewModel is not null)
        {
            await this.ViewModel.LoadProjectCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
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
