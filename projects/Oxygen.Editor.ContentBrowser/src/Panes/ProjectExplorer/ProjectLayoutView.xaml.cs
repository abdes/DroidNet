// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     The View for the Project Layout pane in the <see cref="ContentBrowserView" />.
/// </summary>
[ViewModel(typeof(ProjectLayoutViewModel))]
public sealed partial class ProjectLayoutView
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="ProjectLayoutView" /> class.
    /// </summary>
    public ProjectLayoutView()
    {
        this.InitializeComponent();

        // Inline lifecycle handlers: attach/detach subscriptions concisely.
        this.Loaded += (_, _) =>
        {
            this.ViewModelChanged += this.OnViewModelChanged;
            this.ViewModel?.RenameRequested += this.OnRenameRequested;
        };

        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.OnViewModelChanged;
            this.ViewModel?.RenameRequested -= this.OnRenameRequested;
        };
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<ProjectLayoutViewModel> args)
    {
        _ = sender;

        if (args.OldValue is { } old)
        {
            old.RenameRequested -= this.OnRenameRequested;
        }

        if (this.ViewModel is { } vm)
        {
            vm.RenameRequested += this.OnRenameRequested;
        }
    }

    private async void OnRenameRequested(object? sender, ITreeItem item)
    {
        if (this.ProjectTree is null)
        {
            return;
        }

        _ = await this.ProjectTree.BeginRenameAsync(item).ConfigureAwait(true);
    }
}
