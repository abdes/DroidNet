// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>A view model outlives the trees that show it, as the Content Browser's does when its view is
/// rebuilt; only a loaded tree listens to its rows, so replacing the root rebuilds the live tree.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeRootResetTests : VisualUserInterfaceTests, IDisposable
{
    private TestViewModel? viewModel;

    public TestContext TestContext { get; set; } = null!;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task AnUnloadedTreeStopsListeningToItsRows_Async() => EnqueueAsync(async () =>
    {
        var tree = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        _ = tree.DisplayedItems.Should().BeSameAs(this.viewModel!.ShownItems);

        await UnloadTestContentAsync(tree).ConfigureAwait(true);

        _ = tree.DisplayedItems.Should().BeNull("a discarded view must not keep its repeater subscribed to the rows");
    });

    [TestMethod]
    public Task AReloadedTreeShowsItsRowsAgain_Async() => EnqueueAsync(async () =>
    {
        var tree = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        await UnloadTestContentAsync(tree).ConfigureAwait(true);

        await LoadTestContentAsync(tree).ConfigureAwait(true);

        _ = tree.DisplayedItems.Should().BeSameAs(this.viewModel!.ShownItems);
    });

    [TestMethod]
    public Task ReplacingTheRootRebuildsTheViewThatReplacedADiscardedOne_Async() => EnqueueAsync(async () =>
    {
        var discarded = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(discarded).ConfigureAwait(true);
        await UnloadTestContentAsync(discarded).ConfigureAwait(true);
        var live = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(live).ConfigureAwait(true);

        await this.viewModel!.InitializeAsync(new TestItemAdapter(new TestTreeItem { Label = "R2" }, isRoot: true))
            .ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = live.DisplayedItems.Should().BeSameAs(this.viewModel.ShownItems);
        _ = this.viewModel.ShownItems.Should().ContainSingle().Which.Label.Should().Be("R2");
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var setup = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TestViewModel(this.LoggerFactory);
                await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
                setup.SetResult();
            });
        await setup.Task.ConfigureAwait(true);
    }
}
