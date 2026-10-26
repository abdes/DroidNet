// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// The repeater shows the view model's rows directly. A tree that unloads keeps them, because a closing window unloads
/// it while its XAML is torn down, and its repeater must take every change of the rows while it is out of the tree
/// without stopping the rows from notifying anyone else; loading it again shows the rows as they are then. A view model
/// is shown by one live tree at a time: a second tree loaded over it while the first is still loaded is rejected, and a
/// tree lets go when it unloads so another one can attach.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeLifecycleTests : VisualUserInterfaceTests, IDisposable
{
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task RowsChangedWhileTheTreeIsUnloadedShowWhenItLoadsAgain_Async() => EnqueueAsync(async () =>
    {
        var tree = new TestableDynamicTree { ViewModel = this.viewModel, Height = 400 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);

        // Subscribed after the repeater, so it is notified only if the repeater's handler did not fail.
        var notifications = 0;
        ((INotifyCollectionChanged)this.viewModel!.ShownItems).CollectionChanged += (_, _) => ++notifications;
        await UnloadTestContentAsync(tree).ConfigureAwait(true);
        _ = tree.DisplayedItems.Should().BeSameAs(this.viewModel.ShownItems, "unloading must not change a XAML property");

        await this.ResetRootAsync("R2").ConfigureAwait(true);
        var afterReset = notifications;
        await this.ResetRootAsync("R3").ConfigureAwait(true);

        _ = afterReset.Should().BePositive("the rows must keep notifying while an unloaded repeater listens");
        _ = notifications.Should().BeGreaterThan(afterReset);

        await LoadTestContentAsync(tree).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = tree.DisplayedItems.Should().BeSameAs(this.viewModel.ShownItems);
        _ = TreeRenderHelpers.Rows(tree).Select(static row => row.ItemAdapter!.Label).Should().Equal("R3");
    });

    [TestMethod]
    public Task ClearingTheViewModelOfAnUnloadedTreeReleasesItsRows_Async() => EnqueueAsync(async () =>
    {
        var tree = new TestableDynamicTree { ViewModel = this.viewModel, Height = 400 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        await UnloadTestContentAsync(tree).ConfigureAwait(true);

        // A host releases an unloaded view by clearing its view model; the tree lets go of the rows then.
        tree.ViewModel = null;

        _ = tree.DisplayedItems.Should().BeNull();
    });

    [TestMethod]
    public Task ReplacingTheRootRebuildsTheTreeThatLoadedAfterAnUnloadedOne_Async() => EnqueueAsync(async () =>
    {
        var unloaded = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(unloaded).ConfigureAwait(true);
        await UnloadTestContentAsync(unloaded).ConfigureAwait(true);
        var live = new TestableDynamicTree { ViewModel = this.viewModel, Height = 400 };
        await LoadTestContentAsync(live).ConfigureAwait(true);

        await this.ResetRootAsync("R2").ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = live.DisplayedItems.Should().BeSameAs(this.viewModel!.ShownItems);
        _ = TreeRenderHelpers.Rows(live).Select(static row => row.ItemAdapter!.Label).Should().Equal("R2");
    });

    [TestMethod]
    public Task ASecondTreeIsRejectedWhileTheFirstIsLoaded_Async() => EnqueueAsync(async () =>
    {
        var first = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(first).ConfigureAwait(true);
        var displayed = this.viewModel!.DisplayedInteractionItems;

        // The second tree is not loaded: a loaded one is refused from its Loaded handler, so the attach is checked at
        // the call a loading tree makes.
        var second = new TestableDynamicTree { ViewModel = this.viewModel };

        _ = this.viewModel.TryAttachTree(second).Should().BeFalse("a view model has one live tree");
        _ = this.viewModel.IsAttachedTree(first).Should().BeTrue();
        _ = this.viewModel.DisplayedInteractionItems.Should().BeSameAs(displayed);
        _ = TreeRenderHelpers.Rows(first).Should().NotBeEmpty();

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
        _ = TreeRenderHelpers.Rows(second).Should().NotBeEmpty();
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

    private Task ResetRootAsync(string label)
        => this.viewModel!.InitializeAsync(new TestItemAdapter(new TestTreeItem { Label = label }, isRoot: true));
}
