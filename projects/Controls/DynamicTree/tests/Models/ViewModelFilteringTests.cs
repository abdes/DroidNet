// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Filtering")]
public sealed partial class ViewModelFilteringTests : ViewModelTestBase, IDisposable
{
    private TestViewModel viewModel = null!;

    public void Dispose() => this.viewModel.Dispose();

    [TestInitialize]
    public void Initialize() => this.viewModel = new TestViewModel(skipRoot: false, loggerFactory: this.LoggerFactoryInstance);

    [TestMethod]
    public async Task FilteredItems_NullPredicate_EqualsShownItems()
    {
        // Arrange
        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Act
        var filtered = this.viewModel.FilteredItems.ToList();
        var shown = this.viewModel.ShownItems.ToList();

        // Assert
        _ = filtered.Should().Equal(shown);
    }

    [TestMethod]
    [DataRow("GC1", "R,R-C1,R-C1-GC1,R-C2,R-C2-GC1")]
    [DataRow("__NO_MATCH__", "")]
    public async Task FilteredItems_WithPredicate_IncludesMatchesPlusAncestors_InPreOrder(string text, string expectedLabels)
    {
        // Arrange
        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        this.viewModel.FilterPredicate = item => item.Label.Contains(text, StringComparison.Ordinal);

        // Act
        var labels = this.viewModel.FilteredItems.Select(i => i.Label).ToList();

        // Assert
        _ = labels.Should().Equal(expectedLabels.Split(',', StringSplitOptions.RemoveEmptyEntries));
        _ = this.viewModel.ShownItemsCount.Should().Be(7, "filtering never changes ShownItems");
    }

    [TestMethod]
    public async Task FilteredItems_ClearingPredicate_RestoresAllShownItems()
    {
        // Arrange
        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
        _ = this.viewModel.FilteredItems.Should().HaveCount(5);

        // Act
        this.viewModel.FilterPredicate = null;

        // Assert
        _ = this.viewModel.FilteredItems.Should().Equal(this.viewModel.ShownItems);
    }

    [TestMethod]
    public async Task FilteredItems_ReplacingPredicate_AppliesTheNewPredicate()
    {
        // Arrange
        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
        _ = this.viewModel.FilteredItems.Should().HaveCount(5);

        // Act
        this.viewModel.FilterPredicate = item => item.Label.Contains("GC2", StringComparison.Ordinal);

        // Assert
        _ = this.viewModel.FilteredItems.Select(i => i.Label).Should().Equal("R", "R-C1", "R-C1-GC2");
    }

    [TestMethod]
    public async Task FilteredItems_RefreshesOnItemPropertyChanges_WhenPredicateUsesAnyProperty()
    {
        // Arrange
        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        // Match dimmed items only; nothing is locked yet. Reading the view subscribes to the items.
        this.viewModel.FilterPredicate = item => item is TreeItemAdapter { IsDimmed: true };
        _ = this.viewModel.FilteredItems.Should().BeEmpty();

        var grandChild = this.viewModel.ShownItems.First(i => string.Equals(i.Label, "R-C1-GC2", StringComparison.Ordinal));

        // Act
        ((TreeItemAdapter)grandChild).IsDimmed = true;
        this.viewModel.RefreshFiltering();

        // Assert
        _ = this.viewModel.FilteredItems.Select(i => i.Label).Should().Equal("R", "R-C1", "R-C1-GC2");
    }

    [TestMethod]
    public async Task FilteredItems_UpdatesWhenShownItemsChange_AfterExpansion()
    {
        // Arrange
        var root = CreateTreeWithCollapsedRoot();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        this.viewModel.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
        _ = this.viewModel.FilteredItems.Should().BeEmpty();

        // Act
        await this.viewModel.ExpandItemAsync(root).ConfigureAwait(false);
        this.viewModel.RefreshFiltering();

        // Assert
        _ = this.viewModel.FilteredItems.Select(i => i.Label).Should().Equal(
            "R",
            "R-C1",
            "R-C1-GC1",
            "R-C2",
            "R-C2-GC1");
        _ = this.viewModel.ShownItemsCount.Should().Be(7);
    }

    [TestMethod]
    public async Task FilteredItems_WithPredicate_UsingCombinedProperties_IncludesMatchesPlusAncestors()
    {
        // Arrange
        var root = CreateExpandedTreeWithLockedScene3();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        this.viewModel.FilterPredicate = item => item.IsLocked && item.Label.Contains("Scene 3", StringComparison.Ordinal);

        // Act
        var labels = this.viewModel.FilteredItems.Select(i => i.Label).ToList();

        // Assert
        _ = this.viewModel.ShownItemsCount.Should().Be(7);
        _ = labels.Should().Equal(
            "R",
            "R-C1",
            "Scene 3");
    }

    [TestMethod]
    public async Task FilteredItems_CollapsingParentOfLoadedMatchingDescendant_KeepsParentVisible()
    {
        // Arrange
        var match = new TestTreeItemAdapter { Label = "MATCH" };
        var parent = new TestTreeItemAdapter([match]) { Label = "P", IsExpanded = true };
        var root = new TestTreeItemAdapter([parent], isRoot: true) { Label = "R", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        this.viewModel.FilterPredicate = item => item.Label.Contains("MATCH", StringComparison.Ordinal);

        _ = this.viewModel.FilteredItems.Select(i => i.Label).Should().Equal("R", "P", "MATCH");

        // Act: collapse the parent so the matching descendant is removed from ShownItems but remains loaded.
        await this.viewModel.CollapseItemAsync(parent).ConfigureAwait(false);
        this.viewModel.RefreshFiltering();

        // Assert: parent remains visible due to loaded-only subtree match.
        var labels = this.viewModel.FilteredItems.Select(i => i.Label).ToList();
        _ = labels.Should().Equal(
            "R",
            "P");
    }

    [TestMethod]
    public async Task FilteredItems_UnloadedSubtree_DoesNotMatchUntilLoaded()
    {
        // Arrange: descendant exists but is not loaded (parent not expanded).
        var match = new TestTreeItemAdapter { Label = "MATCH" };
        var parent = new TestTreeItemAdapter([match]) { Label = "P", IsExpanded = false };
        var root = new TestTreeItemAdapter([parent], isRoot: true) { Label = "R", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        this.viewModel.FilterPredicate = item => item.Label.Contains("MATCH", StringComparison.Ordinal);

        // Act / Assert (unloaded subtree treated as no-match; reading the view also subscribes to the items)
        _ = this.viewModel.FilteredItems.Should().BeEmpty();

        // Act: expand parent, which loads the matching descendant.
        await this.viewModel.ExpandItemAsync(parent).ConfigureAwait(false);
        this.viewModel.RefreshFiltering();

        // Assert: now the match is loaded and the lineage becomes visible.
        var labels = this.viewModel.FilteredItems.Select(i => i.Label).ToList();
        _ = labels.Should().Equal(
            "R",
            "P",
            "MATCH");
    }

    [TestMethod]
    public async Task RefreshFiltering_AfterAPredicateThrew_RecomputesTheWholeCache()
    {
        // Arrange: the predicate fails on one row only while it is told to.
        var throwing = false;
        bool Predicate(ITreeItem item)
        {
            if (throwing && string.Equals(item.Label, "R-C2", StringComparison.Ordinal))
            {
                throw new InvalidOperationException("predicate failure");
            }

            return item.Label.Contains("GC1", StringComparison.Ordinal);
        }

        var root = CreateExpandedTree();
        await this.viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        this.viewModel.FilterPredicate = new Predicate<ITreeItem>(Predicate);
        _ = this.viewModel.FilteredItems.Should().HaveCount(5);

        throwing = true;
        var replace = () => this.viewModel.FilterPredicate = new Predicate<ITreeItem>(Predicate);
        _ = replace.Should().Throw<InvalidOperationException>();

        // Act
        throwing = false;
        this.viewModel.RefreshFiltering();

        // Assert
        _ = this.viewModel.FilteredItems.Select(item => item.Label)
            .Should().Equal("R", "R-C1", "R-C1-GC1", "R-C2", "R-C2-GC1");
    }

    private static TestTreeItemAdapter CreateExpandedTree()
    {
        var c1gc1 = new TestTreeItemAdapter { Label = "R-C1-GC1" };
        var c1gc2 = new TestTreeItemAdapter { Label = "R-C1-GC2" };
        var c2gc1 = new TestTreeItemAdapter { Label = "R-C2-GC1" };

        var c1 = new TestTreeItemAdapter([c1gc1, c1gc2]) { Label = "R-C1", IsExpanded = true };
        var c2 = new TestTreeItemAdapter([c2gc1]) { Label = "R-C2", IsExpanded = true };
        var c3 = new TestTreeItemAdapter { Label = "R-C3" };

        return new TestTreeItemAdapter([c1, c2, c3], isRoot: true) { Label = "R", IsExpanded = true };
    }

    private static TestTreeItemAdapter CreateTreeWithCollapsedRoot()
    {
        var root = CreateExpandedTree();
        root.IsExpanded = false;
        return root;
    }

    private static TestTreeItemAdapter CreateExpandedTreeWithLockedScene3()
    {
        var scene2 = new TestTreeItemAdapter { Label = "Scene 2" };
        var scene3 = new TestTreeItemAdapter { Label = "Scene 3", IsLocked = true };
        var other = new TestTreeItemAdapter { Label = "Other" };

        var c1 = new TestTreeItemAdapter([scene2, scene3]) { Label = "R-C1", IsExpanded = true };
        var c2 = new TestTreeItemAdapter([other]) { Label = "R-C2", IsExpanded = true };
        var c3 = new TestTreeItemAdapter { Label = "R-C3" };

        return new TestTreeItemAdapter([c1, c2, c3], isRoot: true) { Label = "R", IsExpanded = true };
    }
}
