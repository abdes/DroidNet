// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using Windows.UI.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     The View for the Project Layout pane in the <see cref="Shell.ContentBrowserView" />.
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
            this.ProjectTree.ItemContextRequested += this.OnItemContextRequested;
            this.ProjectTree.KeyDown += this.OnTreeKeyDown;
        };

        this.Unloaded += (_, _) =>
        {
            this.ViewModelChanged -= this.OnViewModelChanged;
            this.ViewModel?.RenameRequested -= this.OnRenameRequested;
            this.ProjectTree.ItemContextRequested -= this.OnItemContextRequested;
            this.ProjectTree.KeyDown -= this.OnTreeKeyDown;
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

    private static bool IsControlDown()
        => Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control).HasFlag(CoreVirtualKeyStates.Down);

    // Shown as the Scene Explorer shows its menu: on the row's item part, focused, at the pointer.
    private void OnItemContextRequested(object? sender, TreeItemContextRequestedEventArgs args)
    {
        if (this.ViewModel is not { } model || model.BuildFolderMenu(args.Item) is not { } menu || !menu.Items.Any())
        {
            return;
        }

        args.Handled = true;
        _ = model.FocusItem(args.Item, DynamicTreeViewModel.RequestOrigin.Programmatic);
        var anchor = (args.Anchor.FindName(DynamicTree.TreeItemPart) as FrameworkElement) ?? args.Anchor;
        var position = args.Position;
        if (position is { } pointer && !ReferenceEquals(anchor, args.Anchor))
        {
            position = args.Anchor.TransformToVisual(anchor).TransformPoint(pointer);
        }

        if (anchor is Control control)
        {
            _ = control.Focus(FocusState.Programmatic);
        }

        _ = ContextMenu.Show(anchor, menu, position);
    }

    // The tree leaves Delete and, with nothing on its own clipboard, Ctrl+V unhandled; the asset list's shortcuts are
    // scoped to the asset list, so the tree handles both here.
    private async void OnTreeKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Handled || this.ViewModel is not { } model)
        {
            return;
        }

        if (args.Key == VirtualKey.Delete)
        {
            args.Handled = true;
            await model.DeleteSelectedFoldersAsync().ConfigureAwait(true);
        }
        else if (args.Key == VirtualKey.V && IsControlDown())
        {
            args.Handled = true;
            await model.PasteIntoFocusedFolderAsync().ConfigureAwait(true);
        }
    }

    private async void OnRenameRequested(object? sender, ProjectItemRenameRequestedEventArgs args)
    {
        if (this.ProjectTree is null)
        {
            return;
        }

        _ = await this.ProjectTree.BeginRenameAsync(args.Item).ConfigureAwait(true);
    }
}
