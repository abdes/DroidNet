// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>A view model outlives the trees that show it, as the Content Browser's does when its view is
/// rebuilt; only a loaded tree listens to its rows, so replacing the root rebuilds the live tree. Unloading stops the
/// listening without changing the repeater's source, because a closing window unloads while its XAML is torn down
/// (see <c>DisplayedTreeItems</c>).</summary>
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
        var view = Displayed(tree);
        _ = view.Source.Should().BeSameAs(this.viewModel!.ShownItems);
        _ = view.IsAttached.Should().BeTrue();

        await UnloadTestContentAsync(tree).ConfigureAwait(true);

        _ = view.IsAttached.Should().BeFalse("a discarded view must not keep its repeater listening to the rows");
        _ = tree.DisplayedItems.Should().BeSameAs(view, "unloading must not change a XAML property");
    });

    [TestMethod]
    public Task AReloadedTreeShowsItsRowsAgain_Async() => EnqueueAsync(async () =>
    {
        var tree = new TestableDynamicTree { ViewModel = this.viewModel };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        await UnloadTestContentAsync(tree).ConfigureAwait(true);

        await LoadTestContentAsync(tree).ConfigureAwait(true);

        _ = Displayed(tree).Source.Should().BeSameAs(this.viewModel!.ShownItems);
        _ = Displayed(tree).IsAttached.Should().BeTrue();
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

        _ = Displayed(live).Source.Should().BeSameAs(this.viewModel.ShownItems);
        _ = Displayed(discarded).IsAttached.Should().BeFalse();
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

    private static DisplayedTreeItems Displayed(DynamicTree tree) => (DisplayedTreeItems)tree.DisplayedItems!;
}
