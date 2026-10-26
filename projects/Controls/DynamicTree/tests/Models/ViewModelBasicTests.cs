// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Initialize")]
public class ViewModelBasicTests : ViewModelTestBase
{
    [TestMethod]
    [DataRow(false, false, "Root")]
    [DataRow(false, true, "Root,Child")]
    [DataRow(true, false, "Child")]
    public async Task InitializeRootAsync_ShowsTheRootAndItsExpandedChildren(bool skipRoot, bool rootIsExpanded, string expectedLabels)
    {
        // Arrange
        using var viewModel = new TestViewModel(skipRoot);
        var childItem = new TestTreeItemAdapter { Label = "Child" };
        var rootItem = new TestTreeItemAdapter([childItem], isRoot: true) { Label = "Root", IsExpanded = rootIsExpanded };

        // Act
        await viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Assert: a skipped root shows its children regardless of its expansion state.
        _ = viewModel.ShownItems.Select(item => item.Label).Should().Equal(expectedLabels.Split(','));
    }
}
