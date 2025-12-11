// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Focus")]
public class ViewModelFocusForceRaiseTests : ViewModelTestBase
{
    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Focus / ForceRaise")]
    public async Task FocusItem_ForceRaise_RaisesPropertyChangedEvenWhenUnchanged()
    {
        // Arrange
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        int count = 0;
        viewModel.PropertyChanged += (_, e) => { if (string.Equals(e.PropertyName, nameof(DynamicTreeViewModel.FocusedItem), System.StringComparison.Ordinal)) count++; };

        // Act
        var result1 = viewModel.FocusItem(item: null, forceRaise: false);
        var result2 = viewModel.FocusItem(item: null, forceRaise: true);

        // Assert
        _ = result1.Should().BeFalse();
        _ = result2.Should().BeTrue();
        _ = count.Should().BeGreaterThan(0);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / Focus / ForceRaise")]
    public async Task ExpandFocusedItemAsync_ReassertsFocusWithForceRaise()
    {
        // Arrange
        var child = new TestTreeItemAdapter { Label = "Child" };
        var root = new TestTreeItemAdapter([child], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        _ = viewModel.FocusItem(child);
        int count = 0;
        viewModel.PropertyChanged += (_, e) => { if (string.Equals(e.PropertyName, nameof(DynamicTreeViewModel.FocusedItem), System.StringComparison.Ordinal)) count++; };

        // Act
        var expanded = await viewModel.ExpandFocusedItemAsync().ConfigureAwait(false);

        // Assert
        _ = expanded.Should().BeTrue();
        _ = count.Should().BeGreaterThan(0);
    }
}
