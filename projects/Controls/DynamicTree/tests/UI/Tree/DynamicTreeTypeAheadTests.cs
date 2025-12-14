// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Windows.System;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree / UI / TypeAhead")]
public sealed partial class DynamicTreeTypeAheadTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TypeAheadViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    [TestCategory("DynamicTree / UI / TypeAhead / Focus")]
    public Task TypingLetter_FocusesBestMatch_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            _ = vm.TryGetFocusedItem(out var focusedBefore, out var originBefore).Should().BeTrue();
            _ = focusedBefore!.Label.Should().Be("ALPHA");
            _ = originBefore.Should().Be(RequestOrigin.PointerInput);

            var handled = await this.tree!.InvokeHandleKeyDownAsync(VirtualKey.B).ConfigureAwait(true);
            _ = handled.Should().BeTrue();

            _ = vm.TryGetFocusedItem(out var focusedAfter, out var originAfter).Should().BeTrue();
            _ = focusedAfter!.Label.Should().Be("BETA");
            _ = originAfter.Should().Be(RequestOrigin.KeyboardInput);
        });

    [TestMethod]
    [TestCategory("DynamicTree / UI / TypeAhead / Buffer")]
    public Task Escape_WhenBufferHasText_ClearsBufferAndDoesNotChangeFocus_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            _ = await this.tree!.InvokeHandleKeyDownAsync(VirtualKey.B).ConfigureAwait(true);
            _ = vm.TryGetFocusedItem(out var focusedAfterTyping, out _).Should().BeTrue();
            _ = focusedAfterTyping!.Label.Should().Be("BETA");

            var handledEscape = await this.tree.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true);
            _ = handledEscape.Should().BeTrue();

            _ = vm.TryGetFocusedItem(out var focusedAfterEscape, out _).Should().BeTrue();
            _ = focusedAfterEscape!.Label.Should().Be("BETA");

            // A second Escape should not be handled because the buffer is already empty.
            var handledEscapeAgain = await this.tree.InvokeHandleKeyDownAsync(VirtualKey.Escape).ConfigureAwait(true);
            _ = handledEscapeAgain.Should().BeFalse();
        });

    [TestMethod]
    [TestCategory("DynamicTree / UI / TypeAhead / Buffer")]
    public Task Backspace_WhenBufferEmpty_IsNotHandled_Async() => EnqueueAsync(
        async () =>
        {
            var handled = await this.tree!.InvokeHandleKeyDownAsync(VirtualKey.Back).ConfigureAwait(true);
            _ = handled.Should().BeFalse();
        });

    [TestMethod]
    [TestCategory("DynamicTree / UI / TypeAhead / Buffer")]
    public Task Backspace_WhenOneCharacter_ClearsBufferAndIsHandled_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;

            _ = await this.tree!.InvokeHandleKeyDownAsync(VirtualKey.B).ConfigureAwait(true);
            _ = vm.TryGetFocusedItem(out var focusedAfterTyping, out _).Should().BeTrue();
            _ = focusedAfterTyping!.Label.Should().Be("BETA");

            var handledBackspace = await this.tree.InvokeHandleKeyDownAsync(VirtualKey.Back).ConfigureAwait(true);
            _ = handledBackspace.Should().BeTrue();

            // Clearing the buffer should not change focus.
            _ = vm.TryGetFocusedItem(out var focusedAfterBackspace, out _).Should().BeTrue();
            _ = focusedAfterBackspace!.Label.Should().Be("BETA");
        });

    [TestMethod]
    [TestCategory("DynamicTree / UI / TypeAhead / Modifiers")]
    public Task ShiftDoesNotAffectTypeAhead_Async() => EnqueueAsync(
        async () =>
        {
            var handled = await this.tree!.InvokeHandleKeyDownAsync(VirtualKey.B, isControlDown: false, isShiftDown: true).ConfigureAwait(true);
            _ = handled.Should().BeTrue();

            var vm = this.viewModel!;
            _ = vm.TryGetFocusedItem(out var focusedAfter, out _).Should().BeTrue();
            _ = focusedAfter!.Label.Should().Be("BETA");
        });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TypeAheadViewModel(this.LoggerFactory);
                await this.viewModel.LoadTypeAheadTreeAsync().ConfigureAwait(true);

                this.tree = new TestableDynamicTree() { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    private sealed partial class TypeAheadViewModel(ILoggerFactory loggerFactory) : DynamicTreeViewModel(loggerFactory)
    {
        public async Task LoadTypeAheadTreeAsync()
        {
            var rootNode = new TestTreeItem
            {
                Label = "ROOT",
                Children =
                [
                    new TestTreeItem { Label = "ALPHA", Children = [] },
                    new TestTreeItem { Label = "BETA", Children = [] },
                    new TestTreeItem { Label = "GAMMA", Children = [] },
                ],
            };

            var rootItem = new TestItemAdapter(rootNode, isRoot: true, isHidden: false)
            {
                IsExpanded = true,
            };

            await this.InitializeRootAsync(rootItem, skipRoot: true).ConfigureAwait(false);
        }

        public bool TryGetFocusedItem([NotNullWhen(true)] out ITreeItem? item, out RequestOrigin origin)
        {
            var fi = this.FocusedItem;
            if (fi is null)
            {
                item = null;
                origin = default;
                return false;
            }

            item = fi.Item;
            origin = fi.Origin;
            return true;
        }
    }
}
