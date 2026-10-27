// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.Routing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneExplorer.Unit.UI.Tests;

/// <summary>
/// A large instance in the tree (scene instances 6.2): expanding an instance root whose source brings in hundreds of
/// nodes shows every one of them, each realized row naming its own node wherever the tree is scrolled, also after
/// the source's structure is read again while the root is expanded.
/// </summary>
[TestClass]
internal sealed partial class SceneExplorerInstanceRowsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private const int NodeCount = 700;

    private static readonly AssetPath Model = AssetPath.Parse("/Content/Scenes/Burger/Burger.glb");

    [TestMethod]
    public Task ExpandingALargeInstance_RealizesEveryRowWithItsNode() => EnqueueAsync(async () =>
    {
        using var harness = await LargeInstanceHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var tree = await harness.ShowAsync(explorer).ConfigureAwait(true);

        await explorer.ExpandItemAsync(harness.RootRow(explorer)).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        await AssertEveryRowAsync(explorer, tree).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ReadingTheStructureAgainWhileExpanded_KeepsEveryRowWithItsNode() => EnqueueAsync(async () =>
    {
        using var harness = await LargeInstanceHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var tree = await harness.ShowAsync(explorer).ConfigureAwait(true);
        await explorer.ExpandItemAsync(harness.RootRow(explorer)).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        // As when the preview mounts the content again after a cook.
        harness.Structures.NotifyChanged();
        await WaitUntilAsync(
            () => harness.RootRow(explorer).IsExpanded && InstancedRows(explorer).Count == NodeCount,
            () => Describe(harness, explorer)).ConfigureAwait(true);

        await AssertEveryRowAsync(explorer, tree).ConfigureAwait(true);
    });

    [TestMethod]
    public Task ExpandingAndScrollingFast_RealizesEveryRowWithItsNode() => EnqueueAsync(async () =>
    {
        using var harness = await LargeInstanceHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var tree = await harness.ShowAsync(explorer).ConfigureAwait(true);
        var scroller = tree.FindDescendant<ScrollView>()!;

        // Expand, then scroll down at once as a fast mouse wheel does: animated steps, one frame apart.
        await explorer.ExpandItemAsync(harness.RootRow(explorer)).ConfigureAwait(true);
        tree.UpdateLayout();
        for (var step = 1; step <= 10; step++)
        {
            _ = scroller.ScrollTo(0, scroller.ScrollableHeight * step / 10, new ScrollingScrollOptions(ScrollingAnimationMode.Enabled));
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        // The wheel stops at the end, whatever the animations still had in flight.
        await ScrollAsync(scroller, scroller.ScrollableHeight).ConfigureAwait(true);
        for (var frame = 0; frame < 10; frame++)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        var problems = RealizedRowProblems(explorer, tree, out var realized);
        var rowsInView = (int)(scroller.ViewportHeight / tree.ItemRowHeight);
        _ = realized.Should().BeGreaterThanOrEqualTo(rowsInView, DescribeRepeater(explorer, tree, scroller));
        _ = problems.Should().BeEmpty(string.Create(CultureInfo.InvariantCulture, $"every realized row of {realized} shows its node at offset {scroller.VerticalOffset:F0}"));
    });

    [TestMethod]
    public Task ReadingTheStructureAgainWhileScrolledDown_KeepsTheRowsInView() => EnqueueAsync(async () =>
    {
        using var harness = await LargeInstanceHarness.CreateAsync().ConfigureAwait(true);
        using var explorer = await harness.OpenAsync().ConfigureAwait(true);
        var tree = await harness.ShowAsync(explorer).ConfigureAwait(true);
        var scroller = tree.FindDescendant<ScrollView>()!;
        await explorer.ExpandItemAsync(harness.RootRow(explorer)).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        await ScrollAsync(scroller, scroller.ScrollableHeight).ConfigureAwait(true);

        // The rows are replaced while the tree is scrolled far down, as when content is mounted again after a cook.
        harness.Structures.NotifyChanged();
        await WaitUntilAsync(
            () => harness.RootRow(explorer).IsExpanded && InstancedRows(explorer).Count == NodeCount,
            () => Describe(harness, explorer)).ConfigureAwait(true);
        for (var frame = 0; frame < 10; frame++)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        var problems = RealizedRowProblems(explorer, tree, out var realized);
        var rowsInView = (int)(scroller.ViewportHeight / tree.ItemRowHeight);
        _ = realized.Should().BeGreaterThanOrEqualTo(rowsInView, string.Create(CultureInfo.InvariantCulture, $"the rows in view at offset {scroller.VerticalOffset:F0} of {scroller.ScrollableHeight:F0} are realized"));
        _ = problems.Should().BeEmpty(string.Create(CultureInfo.InvariantCulture, $"every realized row of {realized} shows its node at offset {scroller.VerticalOffset:F0}"));
    });

    private static List<InstancedNodeAdapter> InstancedRows(SceneExplorerViewModel explorer)
        => [.. explorer.ShownItems.OfType<InstancedNodeAdapter>()];

    // Every shown item is distinct and in the source's order, and at the top, middle and end of the list every row
    // the repeater realizes shows the item at its index, with that item's name.
    private static async Task AssertEveryRowAsync(SceneExplorerViewModel explorer, DynamicTree tree)
    {
        var shown = explorer.ShownItems.ToList();
        _ = shown.Should().OnlyHaveUniqueItems();
        _ = InstancedRows(explorer).Select(static row => row.Label).Should().Equal(Enumerable.Range(0, NodeCount).Select(NameOf));
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var scroller = tree.FindDescendant<ScrollView>()!;
        foreach (var offset in new[] { 0d, scroller.ScrollableHeight / 2, scroller.ScrollableHeight })
        {
            await ScrollAsync(scroller, offset).ConfigureAwait(true);
            var realized = 0;
            for (var index = 0; index < shown.Count; index++)
            {
                if (repeater.TryGetElement(index) is not FrameworkElement container
                    || container.FindName(DynamicTree.TreeItemPart) is not DynamicTreeItem row)
                {
                    continue;
                }

                realized++;
                _ = row.ItemAdapter.Should().BeSameAs(shown[index], string.Create(CultureInfo.InvariantCulture, $"the row realized at index {index} shows the item there"));
                _ = row.Visibility.Should().Be(Visibility.Visible);
                var name = row.FindDescendant<TextBlock>(block => string.Equals(block.Name, DynamicTreeItem.ItemNamePart, StringComparison.Ordinal))!;
                _ = ((Run)name.Inlines[0]).Text.Should().Be(shown[index].Label, string.Create(CultureInfo.InvariantCulture, $"the row at index {index} names its item"));
            }

            _ = realized.Should().BePositive(string.Create(CultureInfo.InvariantCulture, $"the repeater realizes rows at offset {offset}"));
        }
    }

    // Each realized row that does not show its own item, its name and its glyph, described.
    private static List<string> RealizedRowProblems(SceneExplorerViewModel explorer, DynamicTree tree, out int realized)
    {
        var shown = explorer.ShownItems.ToList();
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var problems = new List<string>();
        realized = 0;
        for (var index = 0; index < shown.Count; index++)
        {
            if (repeater.TryGetElement(index) is not FrameworkElement container
                || container.FindName(DynamicTree.TreeItemPart) is not DynamicTreeItem row)
            {
                continue;
            }

            realized++;
            var label = row.FindDescendant<TextBlock>(block => string.Equals(block.Name, DynamicTreeItem.ItemNamePart, StringComparison.Ordinal));
            var name = label?.Inlines.Count > 0 ? ((Run)label.Inlines[0]).Text : null;
            var thumbnail = row.FindDescendant<ContentPresenter>(presenter => string.Equals(presenter.Name, "PartThumbnailPresenter", StringComparison.Ordinal));
            if (!ReferenceEquals(row.ItemAdapter, shown[index])
                || !string.Equals(name, shown[index].Label, StringComparison.Ordinal)
                || container.Visibility != Visibility.Visible || container.Opacity < 1 || row.Opacity < 1
                || row.ActualHeight <= 0 || label is null || label.ActualWidth <= 0
                || thumbnail is null || thumbnail.ActualWidth <= 0)
            {
                problems.Add(string.Create(
                    CultureInfo.InvariantCulture,
                    $"#{index} '{shown[index].Label}': adapter '{row.ItemAdapter?.Label}', name '{name}', container {container.Visibility}/{container.Opacity}, row {row.Opacity}/{row.ActualHeight:F0}, label width {label?.ActualWidth:F0}, thumbnail width {thumbnail?.ActualWidth:F0}"));
            }
        }

        return problems;
    }

    // What the repeater holds against where the scroll view is: each child element's index, visibility and position.
    private static string DescribeRepeater(SceneExplorerViewModel explorer, DynamicTree tree, ScrollView scroller)
    {
        var repeater = tree.FindDescendant<ItemsRepeater>()!;
        var children = Enumerable.Range(0, VisualTreeHelper.GetChildrenCount(repeater))
            .Select(index => (UIElement)VisualTreeHelper.GetChild(repeater, index))
            .Select(child => string.Create(
                CultureInfo.InvariantCulture,
                $"[{repeater.GetElementIndex(child)} {child.Visibility} y={child.TransformToVisual(repeater).TransformPoint(default).Y:F0}]"));
        return string.Create(
            CultureInfo.InvariantCulture,
            $"offset {scroller.VerticalOffset:F0} of {scroller.ScrollableHeight:F0}, extent {scroller.ExtentHeight:F0}, viewport {scroller.ViewportHeight:F0}; repeater height {repeater.ActualHeight:F0}, shown items {explorer.ShownItems.Count()}; children {string.Join(' ', children)}");
    }

    private static string NameOf(int index) => "Part_" + index.ToString("D3", CultureInfo.InvariantCulture);

    private static async Task ScrollAsync(ScrollView scroller, double offset)
    {
        _ = scroller.ScrollTo(0, offset, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        await WaitUntilAsync(() => Math.Abs(scroller.VerticalOffset - offset) <= 1).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
    }

    private static async Task WaitUntilAsync(Func<bool> condition, Func<string>? describe = null)
    {
        for (var frame = 0; frame < 200 && !condition(); frame++)
        {
            await WaitForRenderAsync().ConfigureAwait(true);
        }

        _ = condition().Should().BeTrue(describe?.Invoke() ?? string.Empty);
    }

    // What the tree shows of the instance: the root row, its children, and the instanced rows among the shown items.
    private static string Describe(LargeInstanceHarness harness, SceneExplorerViewModel explorer)
    {
        var roots = explorer.ShownItems.OfType<SceneNodeAdapter>().Where(row => row.AttachedObject.Id == harness.Root).ToList();
        var instanced = InstancedRows(explorer);
        return string.Create(
            CultureInfo.InvariantCulture,
            $"root rows shown {roots.Count}, expanded {string.Join(',', roots.Select(static row => row.IsExpanded))}, children {string.Join(',', roots.Select(static row => row.ChildrenCount))}; shown items {explorer.ShownItems.Count()}, instanced rows {instanced.Count}, distinct {instanced.Distinct().Count()}, distinct labels {instanced.Select(static row => row.Label).Distinct(StringComparer.Ordinal).Count()}");
    }

    /// <summary>A scene with one instance of a model whose structure holds <see cref="NodeCount"/> root nodes.</summary>
    private sealed partial class LargeInstanceHarness : IDisposable
    {
        private readonly TestSession session;
        private readonly TestDocumentArea documents = new();
        private readonly SceneSelectionService selection = new();

        private LargeInstanceHarness(TestSession session, TestScene document, NodeId root)
        {
            this.session = session;
            this.Document = document;
            this.Root = root;
            _ = this.Structures.Publish(Model, Enumerable.Range(0, NodeCount).Select(index => (NodePath.Of(NodeId.New()), NameOf(index), (NodePath?)null)));
            this.documents.Activate(document.Document);
        }

        public FakeSceneStructures Structures { get; } = new();

        public TestScene Document { get; }

        public NodeId Root { get; }

        public static async Task<LargeInstanceHarness> CreateAsync()
        {
            var project = TestProject.Create(new MockFileSystem(), @"C:\LargeInstanceHarness");
            var document = TestScenes.Open(new Scene { Name = "Diner" }, project: project);
            var node = SceneNodeTemplates.Instance(document.Scene, Model);
            _ = (await document.Authoring.ExecuteAsync(new InsertNodes([SceneNodeTemplates.Append(document.Scene, document.Layout, node)])).ConfigureAwait(true))
                .Succeeded.Should().BeTrue();
            return new LargeInstanceHarness(new TestSession(project.Context), document, node.Id);
        }

        public async Task<SceneExplorerViewModel> OpenAsync()
        {
            var explorer = new SceneExplorerViewModel(
                Mock.Of<IRouter>(),
                this.documents,
                this.Document.Documents,
                this.selection,
                session: this.session,
                structures: this.Structures);
            await explorer.HandleDocumentOpenedAsync(this.Document.Scene).ConfigureAwait(true);
            return explorer;
        }

        // Shows the explorer in a short window, once the instance's structure is read.
        public async Task<DynamicTree> ShowAsync(SceneExplorerViewModel explorer)
        {
            var view = new SceneExplorerView { ViewModel = explorer, Height = 400 };
            await LoadTestContentAsync(view).ConfigureAwait(true);
            await WaitUntilAsync(() => this.RootRow(explorer).ChildrenCount == NodeCount, () => Describe(this, explorer)).ConfigureAwait(true);
            return (DynamicTree)view.FindName("ExplorerTree")!;
        }

        public SceneNodeAdapter RootRow(SceneExplorerViewModel explorer)
            => explorer.ShownItems.OfType<SceneNodeAdapter>().Single(row => row.AttachedObject.Id == this.Root);

        public void Dispose()
        {
            this.Document.Dispose();
            this.session.Dispose();
        }
    }
}
