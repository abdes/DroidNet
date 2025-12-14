// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public class DynamicTreeBasicTests : VisualUserInterfaceTests
{
    private DynamicTree? tree;
    private TestVisualStateManager? vsm;
    private TestViewModel? viewModel;

    [TestMethod]
    public Task InitializesTemplateParts_Async() => EnqueueAsync(
        () =>
        {
            // Assert
            _ = this.tree!.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTree.RootGridPart, StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.tree!.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal)).Should().NotBeNull();
        });

    [TestMethod]
    public Task VerifyInitialTreeStructure_Async() =>
        EnqueueAsync(
        () =>
        {
            // Assert
            _ = this.viewModel.Should().NotBeNull();
            var shownItems = this.viewModel!.ShownItems;
            _ = shownItems.Should().ContainSingle();

            var rootItem = shownItems.FirstOrDefault();
            _ = rootItem.Should().NotBeNull();
            _ = rootItem!.Label.Should().Be("R");
            _ = rootItem.IsExpanded.Should().BeFalse();

            // Verify the tree control only shows one DynamicTreeItem
            var treeItemsCount = CountItemsShownInTree(this.tree!);
            _ = treeItemsCount.Should().Be(1);
        });

    [TestMethod]
    public Task ExpandRootAndChildren_ShouldIncludeAllItemsInItemsRepeater_Async() => EnqueueAsync(
        async () =>
        {
            // Arrange
            var rootItem = this.viewModel!.ShownItems.FirstOrDefault();
            _ = rootItem.Should().NotBeNull();
            _ = rootItem!.IsExpanded.Should().BeFalse();

            // Act
            await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(true);

            foreach (var child in await rootItem.Children.ConfigureAwait(true))
            {
                await this.viewModel.ExpandItemAsync(child).ConfigureAwait(true);
            }

            // Assert
            var itemsRepeater = this.tree!.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = itemsRepeater.Should().NotBeNull();

            var itemsSourceView = itemsRepeater!.ItemsSourceView;
            _ = itemsSourceView.Should().NotBeNull();
            _ = itemsSourceView.Count.Should().Be(this.viewModel.ShownItemsCount);

            for (var i = 0; i < itemsSourceView.Count; i++)
            {
                var item = itemsSourceView.GetAt(i);
                _ = item.Should().Be(this.viewModel.GetShownItemAt(i));
            }
        });

    [TestMethod]
    public Task MoveItems_MultiSelectionPreserved_Async() => EnqueueAsync(
        async () =>
        {
            // Arrange
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);
            var secondChild = (TreeItemAdapter)vm.GetShownItemAt(2);

            vm.ClearAndSelectItem(firstChild);
            vm.SelectItem(secondChild, RequestOrigin.PointerInput);

            // Act
            await vm.MoveItemsAsync([firstChild, secondChild], root, 0).ConfigureAwait(true);

            // Assert
            var selectedItems = vm.ShownItems.Where(item => item.IsSelected).ToList();
            _ = selectedItems.Should().HaveCount(2);
            _ = selectedItems[0].Should().BeSameAs(firstChild);
            _ = selectedItems[1].Should().BeSameAs(secondChild);
        });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TestViewModel();
                await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
                this.viewModel.SelectionMode = SelectionMode.Multiple;
                this.tree = new DynamicTree() { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                var vsmTarget = this.tree.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTree.RootGridPart, StringComparison.Ordinal));
                _ = vsmTarget.Should().NotBeNull();
                this.vsm = new TestVisualStateManager();
                VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    /// <summary>
    /// Counts the number of children of a specific type in an ItemsRepeater within a given tree.
    /// </summary>
    /// <param name="tree">The DynamicTree control.</param>
    /// <returns>The count of children of the specified type.</returns>
    private static int CountItemsShownInTree(DynamicTree tree)
    {
        var itemsRepeater = tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
        _ = itemsRepeater.Should().NotBeNull();

        return itemsRepeater!.ItemsSourceView.Count;
    }
}
