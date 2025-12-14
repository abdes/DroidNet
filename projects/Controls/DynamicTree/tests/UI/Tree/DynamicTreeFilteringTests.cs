// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
[TestCategory("Filtering")]
public sealed partial class DynamicTreeFilteringTests : VisualUserInterfaceTests, IDisposable
{
    private DynamicTree? tree;
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task FilteringDisabled_RendersUnfilteredShownItems_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            // Expand the first two children so grandchildren become part of the shown-items list.
            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = false;

            var repeater = this.tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = repeater.Should().NotBeNull();

            _ = repeater!.ItemsSourceView.Count.Should().Be(vm.ShownItemsCount);
        });

    [TestMethod]
    public Task FilteringEnabled_RendersMatchesPlusAncestors_InPreOrder_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;

            var repeater = this.tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = repeater.Should().NotBeNull();

            var items = repeater!.ItemsSourceView;
            _ = items.Count.Should().Be(5);

            _ = ((ITreeItem)items.GetAt(0)).Label.Should().Be("R");
            _ = ((ITreeItem)items.GetAt(1)).Label.Should().Be("R-C1");
            _ = ((ITreeItem)items.GetAt(2)).Label.Should().Be("R-C1-GC1");
            _ = ((ITreeItem)items.GetAt(3)).Label.Should().Be("R-C2");
            _ = ((ITreeItem)items.GetAt(4)).Label.Should().Be("R-C2-GC1");

            // Filtering must not mutate operation semantics.
            _ = vm.ShownItemsCount.Should().Be(7);
        });

    [TestMethod]
    public Task SelectionCommands_ActOnAllUnfilteredShownItems_EvenWhenFilteringEnabled_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (ITreeItem)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);

            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;

            // Select all should always operate on the unfiltered shown-items list.
            vm.SelectAllCommand.Execute(parameter: null);

            _ = vm.SelectedItemsCount.Should().Be(vm.ShownItemsCount);

            var hiddenGrandChild = vm.ShownItems.First(i => string.Equals(i.Label, "R-C1-GC2", StringComparison.Ordinal));
            _ = hiddenGrandChild.IsSelected.Should().BeTrue();
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

                this.tree = new DynamicTree { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });

        await taskCompletionSource.Task.ConfigureAwait(true);
    }
}
