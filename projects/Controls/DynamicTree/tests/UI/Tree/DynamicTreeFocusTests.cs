// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Windows.System;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree / UI / Handlers")]
public class DynamicTreeFocusTests : VisualUserInterfaceTests
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task OnItemPointerPressed_SelectsItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            _ = root.Should().NotBeNull();

            // Expand root so children are realized in ShownItems
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];

            var testable = this.tree!;
            var handled = testable.InvokeItemPointerPressed(firstChild, isControlDown: false, isShiftDown: false, leftButtonPressed: true);

            _ = handled.Should().BeTrue();
            _ = firstChild.IsSelected.Should().BeTrue();
        });

    [TestMethod]
    public Task OnItemGotFocus_WhenViewModelHadFocus_ReusesOrigin_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            var secondChild = (TreeItemAdapter)vm.ShownItems[2];

            // Set initial focus with a known origin
            _ = vm.FocusItem(firstChild, RequestOrigin.KeyboardInput);

            var testable = this.tree!;
            var handled = testable.InvokeItemGotFocus(secondChild);

            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(secondChild);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
        });

    [TestMethod]
    public Task OnItemGotFocus_WhenNoFocusedItem_SetsProgrammaticOrigin_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];

            // Ensure no focused item exists
            vm.ClearFocus();

            var testable = this.tree!;
            var handled = testable.InvokeItemGotFocus(firstChild);

            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(firstChild);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.Programmatic);
        });

    [TestMethod]
    public Task OnTreeGotFocus_WhenFocusExists_LeavesFocusedItemUnchanged_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // At initialization a focused item already exists (PointerInput origin). Invoking TreeGotFocus
            // should not override an existing focused item.
            var original = vm.FocusedItem;
            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            _ = vm.FocusedItem.Should().BeSameAs(original);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.PointerInput);
        });

    public Task OnTreeGotFocus_WhenNoFocusedItem_FocusesFirstShownItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Clear any existing focus so the tree's GotFocus handler must set it
            vm.ClearFocus();

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            // The ViewModel should have a focused item and it should be the first shown item
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[0]);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
        });

    [TestMethod]
    public Task OnTreeGotFocus_WhenFocusExists_DoesNotFocusSelectedItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Select a non-first item (first child)
            var selected = (TreeItemAdapter)vm.ShownItems[1];
            vm.ClearAndSelectItem(selected);

            // Focus already exists from initialization (root), so GotFocus should not override it
            var original = vm.FocusedItem;

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            _ = vm.FocusedItem.Should().BeSameAs(original);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.PointerInput);
        });

    public Task OnTreeGotFocus_WhenNoFocusedItem_FocusesSelectedItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Select a non-first item (first child) and clear focus so GotFocus must set it
            var selected = (TreeItemAdapter)vm.ShownItems[1];
            vm.ClearAndSelectItem(selected);
            vm.ClearFocus();

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            // The ViewModel should have the selected item as the focused item
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(selected);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
        });

    [TestMethod]
    public Task UpKey_MovesFocusToPreviousItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize root and first child's children
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            await vm.ExpandItemAsync(firstChild).ConfigureAwait(true);

            var testable = this.tree!;

            // Start focused on the first grandchild (index 2)
            var start = (TreeItemAdapter)vm.ShownItems[2];
            _ = vm.FocusItem(start, RequestOrigin.KeyboardInput);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeTrue();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[1]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);

            // Move up to root
            moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeTrue();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[0]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);

            // Cannot move beyond first
            moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeFalse();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[0]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);
        });

    [TestMethod]
    public Task DownKey_MovesFocusToNextItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize root and first child's children
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            await vm.ExpandItemAsync(firstChild).ConfigureAwait(true);

            var testable = this.tree!;

            // Start focused on the first child (index 1)
            var start = (TreeItemAdapter)vm.ShownItems[1];
            _ = vm.FocusItem(start, RequestOrigin.KeyboardInput);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[2]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);

            // Move to last shown item step by step
            while (await testable.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true))
            {
                // iterate
            }

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[^1]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);

            // Cannot move beyond last
            var cant = await testable.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true);
            _ = cant.Should().BeFalse();
        });

    [TestMethod]
    public Task HomeKey_MovesFocusToFirstSibling_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize root and first child's children
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            await vm.ExpandItemAsync(firstChild).ConfigureAwait(true);

            var testable = this.tree!;

            // Focus the second grandchild (R-C1-GC2)
            var secondGrandChild = (TreeItemAdapter)vm.ShownItems[3];
            _ = vm.FocusItem(secondGrandChild, RequestOrigin.KeyboardInput);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Home).ConfigureAwait(true);
            _ = moved.Should().BeTrue();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[2]); // R-C1-GC1
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);
        });

    [TestMethod]
    public Task EndKey_MovesFocusToLastSibling_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize root and first child's children
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            await vm.ExpandItemAsync(firstChild).ConfigureAwait(true);

            var testable = this.tree!;

            // Focus the first grandchild (R-C1-GC1)
            var firstGrandChild = (TreeItemAdapter)vm.ShownItems[2];
            _ = vm.FocusItem(firstGrandChild, RequestOrigin.KeyboardInput);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.End).ConfigureAwait(true);
            _ = moved.Should().BeTrue();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[3]); // R-C1-GC2
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);
        });

    [TestMethod]
    public Task CtrlHome_FocusesFirstShownItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize the tree
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            foreach (var item in vm.ShownItems.OfType<TreeItemAdapter>().ToList())
            {
                if (item != root && item.CanAcceptChildren)
                {
                    await vm.ExpandItemAsync(item).ConfigureAwait(true);
                }
            }

            // Move focus to last item first
            _ = vm.FocusLastVisibleItemInTree(RequestOrigin.KeyboardInput);
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[^1]);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);

            var testable = this.tree!;

            // Simulate Ctrl+Home via the control key handler
            var handled = await testable.InvokeHandleKeyDownAsync(VirtualKey.Home, isControlDown: true, isShiftDown: false).ConfigureAwait(true);
            _ = handled.Should().BeTrue();

            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[0]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);
        });

    [TestMethod]
    public Task CtrlEnd_FocusesLastShownItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Realize the tree
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            foreach (var item in vm.ShownItems.OfType<TreeItemAdapter>().ToList())
            {
                if (item != root && item.CanAcceptChildren)
                {
                    await vm.ExpandItemAsync(item).ConfigureAwait(true);
                }
            }

            // Move focus to first item
            _ = vm.FocusFirstVisibleItemInTree(RequestOrigin.KeyboardInput);
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[0]);
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);

            var testable = this.tree!;

            // Simulate Ctrl+End via the control key handler
            var handled = await testable.InvokeHandleKeyDownAsync(VirtualKey.End, isControlDown: true, isShiftDown: false).ConfigureAwait(true);
            _ = handled.Should().BeTrue();
            await EnqueueAsync(() =>
            {
                _ = vm.FocusedItem.Should().NotBeNull();
                _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.ShownItems[^1]);
                _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
            }).ConfigureAwait(true);
        });

    [TestMethod]
    public Task PointerPressed_Ctrl_TogglesSelection_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            var testable = this.tree!;

            // Ensure nothing is selected
            vm.SelectNoneCommand.Execute(parameter: null);
            _ = firstChild.IsSelected.Should().BeFalse();

            // Ctrl-click should select (toggle on)
            var handled = testable.InvokeItemPointerPressed(firstChild, isControlDown: true, isShiftDown: false, leftButtonPressed: true);
            _ = handled.Should().BeTrue();
            _ = firstChild.IsSelected.Should().BeTrue();

            // Ctrl-click again should clear selection (toggle off)
            handled = testable.InvokeItemPointerPressed(firstChild, isControlDown: true, isShiftDown: false, leftButtonPressed: true);
            _ = handled.Should().BeTrue();
            _ = firstChild.IsSelected.Should().BeFalse();
        });

    [TestMethod]
    public Task PointerPressed_Shift_ExtendsSelection_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            var secondChild = (TreeItemAdapter)vm.ShownItems[2];
            var testable = this.tree!;

            // Select first child
            vm.ClearAndSelectItem(firstChild);
            _ = firstChild.IsSelected.Should().BeTrue();

            // Shift-click second child should extend selection to include both
            var handled = testable.InvokeItemPointerPressed(secondChild, isControlDown: false, isShiftDown: true, leftButtonPressed: true);
            _ = handled.Should().BeTrue();
            _ = firstChild.IsSelected.Should().BeTrue();
            _ = secondChild.IsSelected.Should().BeTrue();
        });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TestViewModel(this.LoggerFactory);
                await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
                this.viewModel.SelectionMode = SelectionMode.Multiple;
                this.tree = new TestableDynamicTree() { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }
}
