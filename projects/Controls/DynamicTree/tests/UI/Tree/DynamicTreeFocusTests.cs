// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Windows.System;

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
    public Task OnItemTapped_SetsFocus_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];

            var testable = this.tree!;
            var handled = testable.InvokeItemTapped(firstChild, isControlDown: false, isShiftDown: false);

            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(firstChild);
        });

    [TestMethod]
    public Task OnItemGotFocus_SetsViewModelFocusedItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            var testable = this.tree!;

            var handled = testable.InvokeItemGotFocus(firstChild, isApplyingFocus: false);

            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(firstChild);
        });

    [TestMethod]
    public Task OnTreeGotFocus_FocusesFirstShownItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Clear any selection to simulate "no selected item"
            vm.SelectNoneCommand.Execute(parameter: null);

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            // The ViewModel should have a focused item and it should be the first shown item
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[0]);
        });

    [TestMethod]
    public Task OnTreeGotFocus_FocusesSelectedItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Select a non-first item (first child)
            var selected = (TreeItemAdapter)vm.ShownItems[1];
            vm.ClearAndSelectItem(selected);

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            // The ViewModel should have the selected item as the focused item
            _ = vm.FocusedItem.Should().BeSameAs(selected);
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
            _ = vm.FocusItem(start);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[1]);

            // Move up to root
            moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[0]);

            // Cannot move beyond first
            moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Up).ConfigureAwait(true);
            _ = moved.Should().BeFalse();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[0]);
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
            _ = vm.FocusItem(start);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[2]);

            // Move to last shown item step by step
            while (await testable.InvokeHandleKeyDownAsync(VirtualKey.Down).ConfigureAwait(true))
            {
                // iterate
            }

            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[^1]);

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
            _ = vm.FocusItem(secondGrandChild);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.Home).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[2]); // R-C1-GC1
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
            _ = vm.FocusItem(firstGrandChild);

            var moved = await testable.InvokeHandleKeyDownAsync(VirtualKey.End).ConfigureAwait(true);
            _ = moved.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[3]); // R-C1-GC2
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
            _ = vm.FocusLastVisibleItemInTree();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[^1]);

            var testable = this.tree!;

            // Simulate Ctrl+Home via the control key handler
            var handled = await testable.InvokeHandleKeyDownAsync(VirtualKey.Home, isControlDown: true, isShiftDown: false).ConfigureAwait(true);
            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[0]);
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
            _ = vm.FocusFirstVisibleItemInTree();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[0]);

            var testable = this.tree!;

            // Simulate Ctrl+End via the control key handler
            var handled = await testable.InvokeHandleKeyDownAsync(VirtualKey.End, isControlDown: true, isShiftDown: false).ConfigureAwait(true);
            _ = handled.Should().BeTrue();
            _ = vm.FocusedItem.Should().BeSameAs(vm.ShownItems[^1]);
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

    [TestMethod]
    public Task Tapped_WithModifiers_DoesNotOverridePointer_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems[0];
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.ShownItems[1];
            var testable = this.tree!;

            // Ensure selection cleared
            vm.SelectNoneCommand.Execute(parameter: null);

            // Tap with Control should return false (letting pointer handlers handle selection)
            var handled = testable.InvokeItemTapped(firstChild, isControlDown: true, isShiftDown: false);
            _ = handled.Should().BeFalse();
            _ = firstChild.IsSelected.Should().BeFalse();

            // Tap with Shift should also return false
            handled = testable.InvokeItemTapped(firstChild, isControlDown: false, isShiftDown: true);
            _ = handled.Should().BeFalse();
            _ = firstChild.IsSelected.Should().BeFalse();
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
