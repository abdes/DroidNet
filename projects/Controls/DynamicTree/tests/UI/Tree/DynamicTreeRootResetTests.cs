// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// The repeater shows the view model's rows directly. A tree that unloads keeps them, because a closing window unloads
/// it while its XAML is torn down, and its repeater must take every change of the rows while it is out of the tree
/// without stopping the rows from notifying anyone else; loading it again shows the rows as they are then.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeRootResetTests : VisualUserInterfaceTests, IDisposable
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

        await this.viewModel.InitializeAsync(new TestItemAdapter(new TestTreeItem { Label = "R2" }, isRoot: true))
            .ConfigureAwait(true);
        var afterReset = notifications;
        await this.viewModel.InitializeAsync(new TestItemAdapter(new TestTreeItem { Label = "R3" }, isRoot: true))
            .ConfigureAwait(true);

        _ = afterReset.Should().BePositive("the rows must keep notifying while an unloaded repeater listens");
        _ = notifications.Should().BeGreaterThan(afterReset);

        await LoadTestContentAsync(tree).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = tree.DisplayedItems.Should().BeSameAs(this.viewModel.ShownItems);
        _ = Rows(tree).Select(static row => row.ItemAdapter!.Label).Should().Equal("R3");
    });

    [TestMethod]
    public Task ATreeWithoutAViewModelShowsNoRows_Async() => EnqueueAsync(async () =>
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

        await this.viewModel!.InitializeAsync(new TestItemAdapter(new TestTreeItem { Label = "R2" }, isRoot: true))
            .ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = live.DisplayedItems.Should().BeSameAs(this.viewModel.ShownItems);
        _ = Rows(live).Select(static row => row.ItemAdapter!.Label).Should().Equal("R2");
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);
        this.viewModel = new TestViewModel(this.LoggerFactory);
        await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
    }

    private static List<TreeItemControl> Rows(DynamicTree tree) => [.. tree.FindDescendants().OfType<TreeItemControl>()];
}
