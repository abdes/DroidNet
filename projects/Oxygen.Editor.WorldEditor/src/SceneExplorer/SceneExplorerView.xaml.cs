// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Menus;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>A hierarchical authoring view of a scene's nodes and logical folders.</summary>
[ViewModel(typeof(SceneExplorerViewModel))]
public sealed partial class SceneExplorerView
{
    private readonly DispatcherTimer contextValidationTimer = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private SceneExplorerViewModel? subscribedViewModel;
    private FrameworkElement? contextAnchor;
    private int contextRequestGeneration;

    /// <summary>Initializes a new instance of the <see cref="SceneExplorerView"/> class.</summary>
    public SceneExplorerView()
    {
        this.InitializeComponent();
        this.Loaded += this.SceneExplorerView_Loaded;
        this.Unloaded += this.SceneExplorerView_Unloaded;
        this.ExplorerTree.ItemContextRequested += this.ExplorerTree_ItemContextRequested;
        this.ExplorerTree.ContextRequested += this.ExplorerTree_ContextRequested;
        this.contextValidationTimer.Tick += this.ContextValidationTimer_Tick;
    }

    private static bool IsTextContext(DependencyObject? source)
    {
        for (var current = source; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is TextBox or RichEditBox)
            {
                return true;
            }
        }

        return false;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Search is an async void UI event boundary; failures are logged instead of becoming unhandled dispatcher exceptions.")]
    private async void SearchBox_TextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
    {
        if (args.Reason != AutoSuggestionBoxTextChangeReason.UserInput || this.ViewModel is not { } model)
        {
            return;
        }

        var query = sender.Text;
        try
        {
            _ = await model.SearchAsync(query).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            model.ReportSearchFailure(exception, query);
        }
    }

    private void OnExplorerSizeChanged(object sender, SizeChangedEventArgs args)
    {
        _ = sender;

        _ = VisualStateManager.GoToState(this, args.NewSize.Width < 360 ? "Compact" : "Standard", useTransitions: false);
    }

    private void SceneExplorerView_Loaded(object sender, RoutedEventArgs e)
    {
        this.subscribedViewModel = this.ViewModel;
        if (this.subscribedViewModel is { } model)
        {
            model.RenameRequested += this.ViewModel_RenameRequested;
            model.FindRequested += this.ViewModel_FindRequested;
            model.ContextMenuInvalidated += this.ViewModel_ContextMenuInvalidated;
        }
    }

    private void SceneExplorerView_Unloaded(object sender, RoutedEventArgs e)
    {
        this.CloseContextMenu();
        if (this.subscribedViewModel is { } model)
        {
            model.RenameRequested -= this.ViewModel_RenameRequested;
            model.FindRequested -= this.ViewModel_FindRequested;
            model.ContextMenuInvalidated -= this.ViewModel_ContextMenuInvalidated;
        }

        this.subscribedViewModel = null;
    }

    private async void ExplorerTree_ItemContextRequested(object? sender, TreeItemContextRequestedEventArgs args)
    {
        // Consume synchronously: the asynchronous selection/rename settlement must not bubble a root menu.
        args.Handled = true;
        await this.OpenContextMenuAsync(args.Item, args.Anchor, args.Position).ConfigureAwait(true);
    }

    private async void ExplorerTree_ContextRequested(UIElement sender, ContextRequestedEventArgs args)
    {
        if (args.Handled || IsTextContext(args.OriginalSource as DependencyObject))
        {
            return;
        }

        args.Handled = true;
        Point? position = args.TryGetPosition(this.ExplorerTree, out var pointer) ? pointer : null;
        await this.OpenContextMenuAsync(item: null, this.ExplorerTree, position).ConfigureAwait(true);
    }

    private async Task OpenContextMenuAsync(ITreeItem? item, FrameworkElement anchor, Point? position)
    {
        this.CloseContextMenu();
        var generation = ++this.contextRequestGeneration;
        var model = this.ViewModel;
        if (model is null || !await this.ExplorerTree.SettleRenameAsync().ConfigureAwait(true)
            || !await model.PrepareContextMenuAsync(item).ConfigureAwait(true)
            || generation != this.contextRequestGeneration || !ReferenceEquals(model, this.ViewModel)
            || (item is not null && !ReferenceEquals(anchor.DataContext, item)))
        {
            return;
        }

        var source = model.BuildContextMenuSource(item);
        if (!source.Items.Any())
        {
            return;
        }

        if (item is not null)
        {
            _ = model.FocusItem(item, RequestOrigin.Programmatic);
        }

        var menuAnchor = (anchor.FindName(DynamicTree.TreeItemPart) as FrameworkElement) ?? anchor;
        if (position is { } pointer && !ReferenceEquals(menuAnchor, anchor))
        {
            position = anchor.TransformToVisual(menuAnchor).TransformPoint(pointer);
        }

        if (menuAnchor is Control control)
        {
            _ = control.Focus(FocusState.Programmatic);
        }

        this.contextAnchor = menuAnchor;
        menuAnchor.DataContextChanged += this.ContextAnchor_DataContextChanged;
        menuAnchor.Unloaded += this.ContextAnchor_Unloaded;
        _ = ContextMenu.Show(menuAnchor, source, position);
        this.contextValidationTimer.Start();
    }

    private void CloseContextMenu()
    {
        this.contextRequestGeneration++;
        this.contextValidationTimer.Stop();
        if (this.contextAnchor is not { } anchor)
        {
            return;
        }

        anchor.DataContextChanged -= this.ContextAnchor_DataContextChanged;
        anchor.Unloaded -= this.ContextAnchor_Unloaded;
        ContextMenu.Close(anchor);
        ContextMenu.SetMenuSource(anchor, value: null);
        this.contextAnchor = null;
    }

    private void ViewModel_ContextMenuInvalidated(object? sender, EventArgs args) => this.CloseContextMenu();

    private void ContextAnchor_DataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args) => this.CloseContextMenu();

    private void ContextAnchor_Unloaded(object sender, RoutedEventArgs args) => this.CloseContextMenu();

    private void ContextValidationTimer_Tick(object? sender, object args)
    {
        if (this.contextAnchor is not { } anchor || !ContextMenu.IsOpen(anchor))
        {
            this.CloseContextMenu();
            return;
        }

        // Project activation and document retirement can happen without a tree selection notification.
        this.ViewModel?.RefreshContextActions();
    }

    private async void ViewModel_RenameRequested(object? sender, RenameRequestedEventArgs? args)
    {
        this.CloseContextMenu();
        if (args?.Item is { } item)
        {
            _ = await this.ExplorerTree.BeginRenameAsync(item).ConfigureAwait(true);
        }
    }

    private void ViewModel_FindRequested(object? sender, EventArgs args)
    {
        this.CloseContextMenu();
        _ = this.SearchBox.Focus(FocusState.Keyboard);
    }

    private bool ShouldConsumeWorkspaceAccelerator()
        => this.contextAnchor is { } anchor && ContextMenu.IsOpen(anchor);

    private bool HasTextFocus()
        => this.XamlRoot is { } root && IsTextContext(FocusManager.GetFocusedElement(root) as DependencyObject);

    private async void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (this.HasTextFocus())
        {
            return;
        }

        args.Handled = true;
        if (!this.ShouldConsumeWorkspaceAccelerator() && this.ViewModel is { } model)
        {
            await model.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }

    private async void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (this.HasTextFocus())
        {
            return;
        }

        args.Handled = true;
        if (!this.ShouldConsumeWorkspaceAccelerator() && this.ViewModel is { } model)
        {
            await model.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }

    private async void DeleteInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (this.HasTextFocus())
        {
            return;
        }

        args.Handled = true;
        if (!this.ShouldConsumeWorkspaceAccelerator() && this.ViewModel?.DeleteAction is IAsyncRelayCommand command
            && command.CanExecute(parameter: null))
        {
            await command.ExecuteAsync(parameter: null).ConfigureAwait(true);
        }
    }
}
