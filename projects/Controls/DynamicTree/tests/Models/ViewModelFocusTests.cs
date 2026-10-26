// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

/// <summary>
/// <c>EnsureFocus</c> keeps the current focus, then falls back to the selected item, then to the first
/// navigable item.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Focus")]
public sealed class ViewModelFocusTests : ViewModelTestBase
{
    [TestMethod]
    public async Task EnsureFocus_WhenAnItemIsFocused_KeepsItAndItsOrigin()
    {
        var (viewModel, _, second) = await this.CreateAsync().ConfigureAwait(false);
        using var disposable = viewModel;
        _ = viewModel.FocusItem(second, RequestOrigin.PointerInput);

        var ensured = viewModel.EnsureFocus(RequestOrigin.KeyboardInput);

        _ = ensured.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out var origin).Should().BeTrue();
        _ = focused.Should().BeSameAs(second);
        _ = origin.Should().Be(RequestOrigin.PointerInput);
    }

    [TestMethod]
    public async Task EnsureFocus_WithoutFocusButWithSelection_FocusesTheSelectedItem()
    {
        var (viewModel, _, second) = await this.CreateAsync().ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.ClearAndSelectItem(second);
        viewModel.ClearFocus();

        var ensured = viewModel.EnsureFocus(RequestOrigin.KeyboardInput);

        _ = ensured.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out var origin).Should().BeTrue();
        _ = focused.Should().BeSameAs(second);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
    }

    [TestMethod]
    public async Task EnsureFocus_WithoutFocusOrSelection_FocusesTheFirstShownItem()
    {
        var (viewModel, first, _) = await this.CreateAsync().ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.ClearFocus();

        var ensured = viewModel.EnsureFocus(RequestOrigin.KeyboardInput);

        _ = ensured.Should().BeTrue();
        _ = viewModel.TryGetFocusedItem(out var focused, out var origin).Should().BeTrue();
        _ = focused.Should().BeSameAs(first);
        _ = origin.Should().Be(RequestOrigin.KeyboardInput);
    }

    private async Task<(TestViewModel ViewModel, TestTreeItemAdapter First, TestTreeItemAdapter Second)> CreateAsync()
    {
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second" };
        var root = new TestTreeItemAdapter([first, second], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Single };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        return (viewModel, first, second);
    }
}
