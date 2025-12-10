// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests.DynamicTree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public partial class DynamicTreeTests : VisualUserInterfaceTests
{
    private Controls.DynamicTree? tree;
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
            _ = shownItems.Count.Should().Be(1);

            var rootItem = shownItems.FirstOrDefault();
            _ = rootItem.Should().NotBeNull();
            _ = rootItem!.Label.Should().Be("Root");
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
            _ = itemsSourceView.Count.Should().Be(this.viewModel.ShownItems.Count);

            for (var i = 0; i < itemsSourceView.Count; i++)
            {
                var item = itemsSourceView.GetAt(i);
                _ = item.Should().Be(this.viewModel.ShownItems[i]);
            }
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
                this.tree = new Controls.DynamicTree() { ViewModel = this.viewModel };
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
    private static int CountItemsShownInTree(Controls.DynamicTree tree)
    {
        var itemsRepeater = tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
        _ = itemsRepeater.Should().NotBeNull();

        return itemsRepeater!.ItemsSourceView.Count;
    }

    private sealed class TestTreeItem
    {
        public string Label { get; set; } = string.Empty;

        public IList<TestTreeItem> Children { get; init; } = [];
    }

    private sealed partial class TestViewModel : DynamicTreeViewModel
    {
        /// <summary>
        /// Loads the tree structure asynchronously.
        /// </summary>
        /// <returns>A task representing the asynchronous operation.</returns>
        public async Task LoadTreeStructureAsync()
        {
            // Example of loading the tree structure
            var rootNode = new TestTreeItem
            {
                Label = "Root",
                Children =
                [
                    new TestTreeItem { Label = "Child 1", Children = [] },
                    new TestTreeItem
                    {
                        Label = "Child 2",
                        Children =
                        [
                            new TestTreeItem { Label = "Sub - Child 1", Children = [] },
                        ],
                    },
                ],
            };

            var rootItem = new TestItemAdapter(rootNode, isRoot: true, isHidden: false);
            await this.InitializeRootAsync(rootItem, skipRoot: false).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// A <see cref="DynamicTree" /> item adapter for testing purposes.
    /// </summary>
    private sealed partial class TestItemAdapter(TestTreeItem node, bool isRoot = false, bool isHidden = false)
        : TreeItemAdapter(isRoot, isHidden)
    {
        /// <inheritdoc/>
        public override string Label
        {
            get => node.Label;
            set
            {
                if (string.Equals(value, node.Label, StringComparison.Ordinal))
                {
                    return;
                }

                node.Label = value;
                this.OnPropertyChanged();
            }
        }

        /// <inheritdoc/>
        public override bool ValidateItemName(string name) => !string.IsNullOrWhiteSpace(name);

        /// <inheritdoc/>
        protected override int DoGetChildrenCount() => node.Children.Count;

        /// <inheritdoc/>
        protected override async Task LoadChildren()
        {
            foreach (var childNode in node.Children)
            {
                this.AddChildInternal(new TestItemAdapter(childNode));
            }

            // Simulate async operation
            await Task.Delay(100).ConfigureAwait(false);
        }
    }
}
