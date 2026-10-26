// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// A view model is shown by one live tree at a time: a second tree loaded over it while the first is still loaded is
/// rejected, and a tree lets go when it unloads so another one can attach.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeSingleViewTests : VisualUserInterfaceTests, IDisposable
{
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    /// <summary>
    /// A loading tree attaches through <see cref="DynamicTreeViewModel.TryAttachTree"/>; Debug builds throw there, which a
    /// loaded tree would raise from its Loaded handler as an unhandled UI exception, so the rule is checked at that call.
    /// Release builds refuse the attach and keep the first tree.
    /// </summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task ASecondTreeIsRejectedWhileTheFirstIsLoaded_Async() => EnqueueAsync(async () =>
    {
        var first = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(first).ConfigureAwait(true);
        var displayed = this.viewModel!.DisplayedInteractionItems;
        var second = new TestableDynamicTree { ViewModel = this.viewModel };

#if DEBUG
        var attach = () => this.viewModel.TryAttachTree(second);
        _ = attach.Should().Throw<InvalidOperationException>();
#else
        _ = this.viewModel.TryAttachTree(second).Should().BeFalse();
#endif
        _ = this.viewModel.IsAttachedTree(first).Should().BeTrue();
        _ = this.viewModel.DisplayedInteractionItems.Should().BeSameAs(displayed);
        _ = Rows(first).Should().NotBeEmpty();

        this.viewModel.DetachTree(second);

        _ = this.viewModel.IsAttachedTree(first).Should().BeTrue("a rejected tree that lets go leaves the attached one");
        _ = this.viewModel.DisplayedInteractionItems.Should().BeSameAs(displayed);
    });

    [TestMethod]
    public Task ATreeAttachesOnceThePreviousOneUnloaded_Async() => EnqueueAsync(async () =>
    {
        var first = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(first).ConfigureAwait(true);
        await UnloadTestContentAsync(first).ConfigureAwait(true);
        _ = this.viewModel!.DisplayedInteractionItems.Should().BeNull("an unloaded tree lets go of its displayed source");

        var second = new TestableDynamicTree { ViewModel = this.viewModel, Height = 400 };
        await LoadTestContentAsync(second).ConfigureAwait(true);

        _ = this.viewModel.IsAttachedTree(second).Should().BeTrue();
        _ = this.viewModel.DisplayedInteractionItems.Should().NotBeNull();
        _ = Rows(second).Should().NotBeEmpty();
    });

    [TestMethod]
    public Task AnUnloadedTreeChangingItsScopeLeavesTheViewModelAlone_Async() => EnqueueAsync(async () =>
    {
        var first = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(first).ConfigureAwait(true);
        await UnloadTestContentAsync(first).ConfigureAwait(true);

        first.SelectionScope = TreeSelectionScope.DisplayedItems;

        _ = this.viewModel!.InteractionScope.Should().Be(TreeSelectionScope.ShownItems);
        _ = this.viewModel.DisplayedInteractionItems.Should().BeNull();
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);
        this.viewModel = new TestViewModel(this.LoggerFactory);
        await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
    }

    private static List<TreeItemControl> Rows(DynamicTree tree) => [.. tree.FindDescendants().OfType<TreeItemControl>()];
}
