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
        var viewModel = new TestViewModel(skipRoot: true);
        var rootItem = new TestTreeItemAdapter(isRoot: true) { Label = "Root" };
        viewModel.ShownItems.Add(rootItem);

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
        var viewModel = new TestViewModel(skipRoot: false);
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
        var viewModel = new TestViewModel(skipRoot: true);
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
        var viewModel = new TestViewModel(skipRoot: false);
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
