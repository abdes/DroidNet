// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.ApplicationModel.DataTransfer;
using Windows.System;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

public sealed partial class DynamicTreeBasicTests
{
    [TestMethod]
    [DataRow(VirtualKey.Down, false, "R-C2", "R-C3")]
    [DataRow(VirtualKey.Up, false, "R-C3", "R-C2")]
    [DataRow(VirtualKey.Home, false, "R-C3", "R-C2")]
    [DataRow(VirtualKey.End, false, "R-C2", "R-C3")]
    [DataRow(VirtualKey.Home, true, "R-C2", "R")]
    [DataRow(VirtualKey.End, true, "R-C2", "R-C3")]
    public Task DisplayedScope_KeyboardUsesRenderedProjection_Async(
        VirtualKey key, bool controlDown, string initialLabel, string expectedLabel) => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        await vm.ExpandItemAsync(vm.ShownItems.First()).ConfigureAwait(true);
        vm.FilterPredicate = item => item.Label is "R-C2" or "R-C3";
        control.IsFilteringEnabled = true;
        control.SelectionScope = TreeSelectionScope.DisplayedItems;
        _ = vm.FocusItem(
            vm.ShownItems.Single(item => string.Equals(item.Label, initialLabel, StringComparison.Ordinal)),
            RequestOrigin.KeyboardInput);

        _ = await control.InvokeHandleKeyDownAsync(key, isControlDown: controlDown).ConfigureAwait(true);

        _ = vm.FocusedItem.Should().NotBeNull();
        _ = vm.FocusedItem!.Item.Label.Should().Be(expectedLabel);
        _ = vm.FocusedItem.Origin.Should().Be(RequestOrigin.KeyboardInput);
    });

    [TestMethod]
    public Task DisplayedScope_FilteringAndScopeChangesUpdateKeyboardNavigation_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        vm.FilterPredicate = item => string.Equals(item.Label, "R-C2", StringComparison.Ordinal);
        control.SelectionScope = TreeSelectionScope.DisplayedItems;

        foreach (var (filtering, scope, expected) in new[]
        {
            (false, TreeSelectionScope.DisplayedItems, children[0]),
            (true, TreeSelectionScope.DisplayedItems, children[1]),
            (true, TreeSelectionScope.ShownItems, children[0]),
            (true, TreeSelectionScope.DisplayedItems, children[1]),
            (false, TreeSelectionScope.DisplayedItems, children[0]),
        })
        {
            control.IsFilteringEnabled = filtering;
            control.SelectionScope = scope;
            _ = vm.FocusItem(root, RequestOrigin.KeyboardInput);
            _ = await control.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true);
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(expected);
        }
    });

    [TestMethod]
    public Task DisplayedScope_TypeAheadIgnoresHiddenMatch_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        vm.FilterPredicate = item => string.Equals(item.Label, "R-C2", StringComparison.Ordinal);
        control.IsFilteringEnabled = true;
        control.SelectionScope = TreeSelectionScope.DisplayedItems;
        _ = vm.FocusItem(root, RequestOrigin.KeyboardInput);

        _ = await control.InvokeHandleKeyDownAsync(VirtualKey.Number1).ConfigureAwait(true);

        _ = vm.FocusedItem.Should().NotBeNull();
        _ = vm.FocusedItem!.Item.Should().BeSameAs(root);
    });

    [TestMethod]
    public Task DisplayedScope_KeyboardRangeAndSelectAllDoNotSelectHiddenRows_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        vm.FilterPredicate = item => item.Label is "R-C1" or "R-C3";
        control.IsFilteringEnabled = true;
        control.SelectionScope = TreeSelectionScope.DisplayedItems;
        _ = control.InvokeGenericItemPointerPressed(children[0]);
        _ = vm.FocusItem(children[2], RequestOrigin.KeyboardInput);

        _ = await control.InvokeHandleKeyDownAsync(VirtualKey.Enter, isShiftDown: true).ConfigureAwait(true);

        _ = children[0].IsSelected.Should().BeTrue();
        _ = children[1].IsSelected.Should().BeFalse();
        _ = children[2].IsSelected.Should().BeTrue();
        vm.SelectNoneCommand.Execute(parameter: null);
        _ = await control.InvokeHandleKeyDownAsync(VirtualKey.A, isControlDown: true).ConfigureAwait(true);
        _ = vm.ShownItems.Where(item => item.IsSelected).Should().Equal(vm.FilteredItems);
    });

    [TestMethod]
    public Task Recycling_ClearsInteractionStateAndDetachesExpansionHandler_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var child = (await root.Children.ConfigureAwait(true))[0];
        _ = await child.Children.ConfigureAwait(true);
        control.UpdateLayout();
        var repeater = control.FindDescendant<ItemsRepeater>()!;
        var container = (FrameworkElement)repeater.GetOrCreateElement(repeater.ItemsSourceView.IndexOf(child));
        var row = (TreeItemControl)container.FindName(DynamicTree.TreeItemPart);
        _ = row.BeginRename().Should().BeTrue();
        var renamePopup = row.FindDescendant<Popup>()!;
        _ = renamePopup.IsOpen.Should().BeTrue();

        // WinUI cannot synthesize a routed pointer press on the action content in this host.
        typeof(TreeItemControl).GetField("isInteractivePointerActive", BindingFlags.Instance | BindingFlags.NonPublic)!
            .SetValue(row, value: true);
        _ = row.IsInteractiveActionInProgress.Should().BeTrue();
        control.SetDropIndicatorVisual(row, DynamicTree.DropZone.Inside);
        var cleared = false;
        repeater.ElementClearing += (sender, args) =>
        {
            _ = sender;
            if (ReferenceEquals(args.Element, container))
            {
                cleared = true;
                _ = row.IsInteractiveActionInProgress.Should().BeFalse();
                _ = DynamicTree.GetDropIndicator(row).Should().Be(DynamicTree.DropIndicatorPosition.None);
                _ = renamePopup.IsOpen.Should().BeFalse();
            }
        };

        await vm.CollapseItemAsync(root).ConfigureAwait(true);
        control.UpdateLayout();
        _ = cleared.Should().BeTrue();
        row.ItemAdapter = child;
        row.FindDescendant<Controls.Expander>()!.Toggle();
        _ = child.IsExpanded.Should().BeFalse("a cleared row must no longer invoke the tree's expand handler");
        _ = vm.ShownItems.Should().Equal(root);
        row.ItemAdapter = null;

        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        control.UpdateLayout();
        var preparedRow = control.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, child))!;
        preparedRow.FindDescendant<Controls.Expander>()!.Toggle();
        _ = child.IsExpanded.Should().BeTrue();
        _ = vm.ShownItems.Should().Contain((await child.Children.ConfigureAwait(true))[0]);
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task Escape_CancelsDragCueHoverAndPendingDropWithoutMutation_Async(bool copy) => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = (await root.Children.ConfigureAwait(true)).ToArray();
        vm.ClearAndSelectItem(children[0]);
        var shown = vm.ShownItems.ToArray();
        control.UpdateLayout();
        var targetRow = control.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, children[1]))!;
        _ = control.BeginDrag(children[0], copy).Should().BeTrue();
        control.SetDropIndicatorVisual(targetRow, DynamicTree.DropZone.Inside);
        control.ScheduleHoverExpand(children[1]);

        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeTrue();
        _ = DynamicTree.GetDropIndicator(targetRow).Should().Be(DynamicTree.DropIndicatorPosition.None);
        await Task.Delay(800).ConfigureAwait(true);

        _ = children[1].IsExpanded.Should().BeFalse("the 600 ms hover timer was cancelled");
        _ = (await control.CompleteDragAsync(children[1], DynamicTree.DropZone.Inside).ConfigureAwait(true))
            .Should().Be(DataPackageOperation.None);
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(children);
        _ = children[0].Parent.Should().BeSameAs(root);
        _ = children[0].IsSelected.Should().BeTrue();
        _ = vm.ShownItems.Should().Equal(shown);
        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeFalse();
    });

    [TestMethod]
    public Task Escape_DragCancellationPrecedesTypeAheadAndClipboard_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var source = (await root.Children.ConfigureAwait(true))[0];
        await vm.CopyItemAsync(source).ConfigureAwait(true);
        _ = await control.InvokeHandleKeyDownAsync(VirtualKey.Number3).ConfigureAwait(true);
        _ = control.BeginDrag(source, isCopy: false).Should().BeTrue();

        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeTrue();
        _ = vm.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeTrue();
        _ = vm.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeTrue();
        _ = vm.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        _ = (await control.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true)).Should().BeFalse();
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DragCompletion_WithoutCancellation_CommitsMoveOrCopy_Async(bool copy) => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        var source = children[0];
        var target = children[1];
        _ = control.BeginDrag(source, copy).Should().BeTrue();

        var operation = await control.CompleteDragAsync(target, DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(copy ? DataPackageOperation.Copy : DataPackageOperation.Move);
        var result = (await target.Children.ConfigureAwait(true))
            .Single(item => string.Equals(item.Label, source.Label, StringComparison.Ordinal));
        _ = result.Parent.Should().BeSameAs(target);
        _ = source.Parent.Should().BeSameAs(copy ? root : target);
        if (copy)
        {
            _ = result.Should().NotBeSameAs(source);
        }
        else
        {
            _ = result.Should().BeSameAs(source);
        }
    });

    [TestMethod]
    public Task Reload_ReattachesViewModelFocusAndDisplayedSource_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var child = (await root.Children.ConfigureAwait(true))[1];
        for (var cycle = 0; cycle < 2; cycle++)
        {
            await LoadTestContentAsync(new Border()).ConfigureAwait(true);
            await LoadTestContentAsync(control).ConfigureAwait(true);
            vm.FilterPredicate = item => ReferenceEquals(item, child);
            control.IsFilteringEnabled = true;
            control.SelectionScope = TreeSelectionScope.DisplayedItems;
            _ = vm.FocusItem(root, RequestOrigin.Programmatic);
            _ = await control.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(child);
            var row = control.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, child))!;
            _ = FocusManager.GetFocusedElement(control.XamlRoot).Should().BeSameAs(row);
        }
    });

    [TestMethod]
    public Task ViewModelReplacement_ReleasesPreviousDisplayedSource_Async() => EnqueueAsync(async () =>
    {
        var control = this.tree!;
        var previous = this.viewModel!;
        var root = previous.ShownItems.First();
        await previous.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        previous.FilterPredicate = item => ReferenceEquals(item, children[1]);
        control.IsFilteringEnabled = true;
        control.SelectionScope = TreeSelectionScope.DisplayedItems;
        using var replacement = new TestViewModel();
        await replacement.LoadTreeStructureAsync().ConfigureAwait(true);

        control.ViewModel = replacement;
        previous.FilterPredicate = null;
        _ = previous.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = previous.FocusedItem!.Item.Should().BeSameAs(children[2]);
        _ = replacement.InteractionScope.Should().Be(TreeSelectionScope.DisplayedItems);
        control.ViewModel = previous;
    });
}
