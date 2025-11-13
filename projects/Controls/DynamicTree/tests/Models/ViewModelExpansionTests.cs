// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public class ViewModelExpansionTests
{
    private readonly TestViewModel viewModel = new(skipRoot: false);

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Expand")]
    public async Task ExpandItemAsync_ShouldExpandItemIfNotAlreadyExpanded()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root" };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeTrue();
        _ = rootItem.Depth.Should().Be(0);
        _ = item.Depth.Should().Be(rootItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Expand")]
    public async Task ExpandItemAsync_ShouldNotChangeStateIfAlreadyExpanded()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeTrue();
        _ = item.IsExpanded.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Expand")]
    public async Task ExpandItemAsync_ShouldThrowIfParentNotExpanded()
    {
        // Arrange
        var childItem = new TestTreeItemAdapter { Label = "Child", IsExpanded = false };
        var parentItem = new TestTreeItemAdapter([childItem]) { Label = "Parent", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([parentItem], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = async () => await this.viewModel.ExpandItemAsync(childItem).ConfigureAwait(false);

        // Assert
        _ = await act.Should().ThrowAsync<InvalidOperationException>()
            .WithMessage("item not yet visible in the tree, cannot expand it").ConfigureAwait(false);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Expand")]
    public async Task ExpandItemAsync_ShouldRestoreExpandedChildrenOfItem()
    {
        // Arrange
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild", IsExpanded = true };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root" };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().Contain(grandChildItem);
        _ = rootItem.Depth.Should().Be(0);
        _ = childItem.Depth.Should().Be(rootItem.Depth + 1);
        _ = grandChildItem.Depth.Should().Be(childItem.Depth + 1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Collapse")]
    public async Task CollapseItemAsync_ShouldCollapseItemIfNotAlreadyCollapsed()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Collapse")]
    public async Task CollapseItemAsync_ShouldNotChangeStateIfAlreadyCollapsed()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = rootItem.IsExpanded.Should().BeFalse();
        _ = item.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Collapse")]
    public async Task CollapseItemAsync_ShouldHideChildrenOfItem()
    {
        // Arrange
        var grandChildItem = new TestTreeItemAdapter { Label = "GrandChild" };
        var childItem = new TestTreeItemAdapter([grandChildItem]) { Label = "Child", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        await this.viewModel.CollapseItemAsync(rootItem).ConfigureAwait(false);

        // Assert
        _ = this.viewModel.ShownItems.Should().NotContain([childItem, grandChildItem]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / ToggleExpansion")]
    public async Task ToggleExpanded_ShouldExpandItemIfCollapsed()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.ToggleExpandedCommand.Execute(rootItem);

        // Assert
        _ = rootItem.IsExpanded.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / ToggleExpansion")]
    public async Task ToggleExpanded_ShouldCollapseItemIfExpanded()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = true };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.ToggleExpandedCommand.Execute(rootItem);

        // Assert
        _ = rootItem.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / ToggleExpansion")]
    public async Task ToggleExpanded_ShouldExpandAndCollapseItem()
    {
        // Arrange
        var item = new TestTreeItemAdapter { Label = "Item", IsExpanded = false };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.ToggleExpandedCommand.Execute(rootItem);
        this.viewModel.ToggleExpandedCommand.Execute(rootItem);

        // Assert
        _ = rootItem.IsExpanded.Should().BeFalse();
    }
}
