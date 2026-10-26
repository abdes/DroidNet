// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeFocusTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task OnItemPointerPressed_SelectsItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            _ = root.Should().NotBeNull();

            // Expand root so children are realized in ShownItems
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);

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
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);
            var secondChild = (TreeItemAdapter)vm.GetShownItemAt(2);

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
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);

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
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Initializing the root already focused an item. Invoking TreeGotFocus should not override it.
            var original = vm.FocusedItem;
            _ = original.Should().NotBeNull();
            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            _ = vm.FocusedItem.Should().BeSameAs(original);
            _ = vm.FocusedItem!.Origin.Should().Be(original!.Origin);
        });

    [TestMethod]
    public Task OnTreeGotFocus_WhenNoFocusedItem_FocusesFirstShownItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Clear any existing focus so the tree's GotFocus handler must set it
            vm.ClearFocus();

            var testable = this.tree!;

            // Simulate the tree control receiving platform focus
            testable.InvokeTreeGotFocus();

            // The ViewModel should have a focused item and it should be the first shown item
            _ = vm.FocusedItem.Should().NotBeNull();
            _ = vm.FocusedItem!.Item.Should().BeSameAs(vm.GetShownItemAt(0));
            _ = vm.FocusedItem!.Origin.Should().Be(RequestOrigin.KeyboardInput);
        });

    [TestMethod]
    public Task OnTreeGotFocus_WhenNoFocusedItem_FocusesSelectedItem_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            // Ensure the tree items are realized
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Select a non-first item (first child) and clear focus so GotFocus must set it
            var selected = (TreeItemAdapter)vm.GetShownItemAt(1);
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
    public Task PointerPressed_Ctrl_TogglesSelection_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);
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
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);
            var secondChild = (TreeItemAdapter)vm.GetShownItemAt(2);
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
