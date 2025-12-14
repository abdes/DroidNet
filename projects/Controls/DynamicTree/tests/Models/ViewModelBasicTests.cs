// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel")]
public class ViewModelBasicTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Initialize")]
    public async Task InitializeRootAsync_ShouldClearShownItems()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: true);

        // Pre-populate shown items through the public InitializeRoot API
        var existingChild = new TestTreeItemAdapter { Label = "Existing" };
        var initialRoot = new TestTreeItemAdapter([existingChild], isRoot: true) { Label = "InitialRoot", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(initialRoot).ConfigureAwait(false);

        var rootItem = new TestTreeItemAdapter(isRoot: true) { Label = "Root" };

        // Act
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Initialize")]
    public async Task InitializeRootAsync_ShouldAddRootItemIfSkipRootIsFalse()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var rootItem = new TestTreeItemAdapter(isRoot: true) { Label = "Root" };

        // Act
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainSingle().Which.Should().Be(rootItem);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Initialize")]
    public async Task InitializeRootAsync_ShouldAddChildrenIfSkipRootIsTrue()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: true);
        var childItem = new TestTreeItemAdapter { Label = "Child" };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root" };

        // Act
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().ContainSingle().Which.Should().Be(childItem);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Initialize")]
    public async Task InitializeRootAsync_ShouldRestoreExpandedChildrenIfRootIsExpanded()
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot: false);
        var childItem = new TestTreeItemAdapter { Label = "Child" };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root", IsExpanded = true };

        // Act
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert
        _ = viewModel.ShownItems.Should().HaveCount(2);
        _ = viewModel.ShownItems.Should().HaveElementAt(0, rootItem);
        _ = viewModel.ShownItems.Should().HaveElementAt(1, childItem);
    }
}
