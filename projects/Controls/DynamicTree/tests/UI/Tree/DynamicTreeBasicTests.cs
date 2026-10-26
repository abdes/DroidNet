// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using AwesomeAssertions.Execution;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media.Imaging;
using Moq;
using Windows.Foundation;
using Windows.Graphics.Imaging;
using TreeExpander = DroidNet.Controls.Expander;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeBasicTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;

    public TestContext TestContext { get; set; } = null!;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task InitializesTemplateParts_Async() => EnqueueAsync(
        () =>
        {
            // Assert
            _ = this.tree!.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTree.RootGridPart, StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.tree!.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal)).Should().NotBeNull();
        });

    [TestMethod]
    public Task ExpandRootAndChildren_ShouldIncludeAllItemsInItemsRepeater_Async() => EnqueueAsync(
        async () =>
        {
            // Arrange
            var rootItem = this.viewModel!.ShownItems.FirstOrDefault();
            _ = rootItem.Should().NotBeNull();
            _ = rootItem!.IsExpanded.Should().BeFalse();

            // Act
            await this.viewModel.ExpandItemAsync(rootItem).ConfigureAwait(true);

            foreach (var child in await rootItem.Children.ConfigureAwait(true))
            {
                await this.viewModel.ExpandItemAsync(child).ConfigureAwait(true);
            }

            // Assert
            var itemsRepeater = this.tree!.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
            _ = itemsRepeater.Should().NotBeNull();

            var itemsSourceView = itemsRepeater!.ItemsSourceView;
            _ = itemsSourceView.Should().NotBeNull();
            _ = itemsSourceView.Count.Should().Be(this.viewModel.ShownItemsCount);

            for (var i = 0; i < itemsSourceView.Count; i++)
            {
                var item = itemsSourceView.GetAt(i);
                _ = item.Should().Be(this.viewModel.GetShownItemAt(i));
            }
        });

    /// <summary>Renaming a row below the viewport, or one inserted there and renamed at once as New Folder does,
    /// scrolls it into view before the editor opens over its label.</summary>
    /// <param name="insertNew">Whether the renamed row is inserted just before the rename begins.</param>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task InlineRename_OfRowBelowViewport_OpensEditorOverTheVisibleRow_Async(bool insertNew) => EnqueueAsync(
        async () =>
        {
            using var vm = new TestViewModel();
            var root = new TestItemAdapter(
                new TestTreeItem { Label = "R", Children = [.. Enumerable.Range(1, 60).Select(static index => new TestTreeItem { Label = $"C{index}" })] },
                isRoot: true);
            await vm.InitializeAsync(root).ConfigureAwait(true);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var testTree = new TestableDynamicTree { ViewModel = vm, Height = 200 };
            await LoadTestContentAsync(testTree).ConfigureAwait(true);

            ITreeItem target = vm.ShownItems.Last();
            if (insertNew)
            {
                target = new TestItemAdapter(new TestTreeItem { Label = "New folder" });
                await vm.InsertItemAsync(target, root, (await root.Children.ConfigureAwait(true)).Count).ConfigureAwait(true);
            }

            _ = (await testTree.BeginRenameAsync(target).ConfigureAwait(true)).Should().BeTrue();
            await RenderAsync().ConfigureAwait(true);

            var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, target))!;
            var (popup, edit) = GetRenameEditor(row);
            var rowTop = row.TransformToVisual(testTree).TransformPoint(default).Y;
            var editTop = edit.TransformToVisual(testTree).TransformPoint(default).Y;
            _ = popup.IsOpen.Should().BeTrue();
            _ = rowTop.Should().BeInRange(0, testTree.ActualHeight - row.ActualHeight, "the row is scrolled into view");
            _ = editTop.Should().BeApproximately(rowTop, row.ActualHeight, "the editor opens over the row");
        });

    [TestMethod]
    public Task InlineRename_PlacesTheEditedTextOnTheLabelText_Async() => EnqueueAsync(async () =>
    {
        using var vm = new TestViewModel();
        await vm.LoadTreeStructureAsync().ConfigureAwait(true);
        var testTree = new TestableDynamicTree { ViewModel = vm };
        await LoadTestContentAsync(testTree).ConfigureAwait(true);
        await RenderAsync().ConfigureAwait(true);
        var item = vm.ShownItems.First();
        var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;

        // The glyphs actually drawn are compared, not layout positions. Only the left
        // part of the text is measured, so the editor's caret at the end of its text is not mistaken for a glyph.
        var label = FindLabel(row);
        var labelInk = await RenderedInkBoundsAsync(label, testTree, bandWidth: null).ConfigureAwait(true);
        var band = labelInk.Width * 0.6;
        var labelBand = await RenderedInkBoundsAsync(label, testTree, band).ConfigureAwait(true);

        _ = row.BeginRename().Should().BeTrue();
        var (_, editor) = GetRenameEditor(row);
        editor.Select(editor.Text.Length, 0);
        await RenderAsync().ConfigureAwait(true);
        var editorBand = await RenderedInkBoundsAsync(editor, testTree, band).ConfigureAwait(true);

        using var scope = new AssertionScope();
        _ = editorBand.Left.Should().BeApproximately(labelBand.Left, 1, "the edited text starts where the label's text was drawn");
        _ = editorBand.Top.Should().BeApproximately(labelBand.Top, 1, "the edited glyphs rise as high as the label's");
        _ = editorBand.Bottom.Should().BeApproximately(labelBand.Bottom, 1, "the edited glyphs sit on the label's baseline");
    });

    [TestMethod]
    public Task InlineRename_NeverShowsTheClearButton_Async() => EnqueueAsync(async () =>
    {
        using var vm = new TestViewModel();
        await vm.LoadTreeStructureAsync().ConfigureAwait(true);
        var testTree = new TestableDynamicTree { ViewModel = vm };
        await LoadTestContentAsync(testTree).ConfigureAwait(true);
        await RenderAsync().ConfigureAwait(true);
        var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, vm.ShownItems.First()))!;

        _ = row.BeginRename().Should().BeTrue();
        var (_, editor) = GetRenameEditor(row);
        editor.Text = "A name long enough for the text box to offer its clear button";
        await RenderAsync().ConfigureAwait(true);

        var clearButton = editor.FindDescendant<Button>(button => string.Equals(button.Name, "DeleteButton", StringComparison.Ordinal));
        _ = clearButton.Should().NotBeNull("the TextBox template has a clear button");
        _ = clearButton!.Visibility.Should().Be(Visibility.Collapsed);
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task InlineRename_RecycledBackToSameItemIgnoresStaleCompletion_Async(bool throws) => EnqueueAsync(async () =>
    {
        using var vm = new TestViewModel();
        await vm.LoadTreeStructureAsync().ConfigureAwait(true);
        var testTree = new TestableDynamicTree { ViewModel = vm };
        await LoadTestContentAsync(testTree).ConfigureAwait(true);
        await RenderAsync().ConfigureAwait(true);
        var item = vm.ShownItems.First();
        var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;
        var pending = new TaskCompletionSource<TreeItemRenameResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        vm.PendingRename = pending;
        vm.RenameCommitStarted = started;
        _ = row.BeginRename().Should().BeTrue();
        var (popup, editor) = GetRenameEditor(row);
        editor.Text = "Old draft";
        var commit = row.CommitRenameDraftAsync();
        await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = editor.IsReadOnly.Should().BeTrue();
        row.ItemAdapter = null;
        row.ItemAdapter = item;
        var currentPending = new TaskCompletionSource<TreeItemRenameResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        vm.PendingRename = currentPending;
        _ = row.BeginRename().Should().BeTrue();
        editor.Text = "Current draft";
        var currentCommit = row.CommitRenameDraftAsync();
        if (throws)
        {
            pending.SetException(new InvalidOperationException("Stale error"));
        }
        else
        {
            pending.SetResult(TreeItemRenameResult.Success);
        }

        await RenderAsync().ConfigureAwait(true);
        await commit.ConfigureAwait(true);
        _ = popup.IsOpen.Should().BeTrue();
        _ = editor.Text.Should().Be("Current draft");
        _ = currentPending.TrySetResult(TreeItemRenameResult.Success);
        await currentCommit.ConfigureAwait(true);
    });

    [TestMethod]
    public Task TrailingContentSelector_UpdatesRealizedRowsAndClearsThemWhenRemoved_Async() => EnqueueAsync(
        async () =>
        {
            var root = this.viewModel!.ShownItems.First();
            this.tree!.TrailingContentWidth = 72;
            this.tree.TrailingContentTemplateSelector = new TestTrailingContentSelector(CreateTemplate("<Button Content='Action' />"));
            await RenderAsync().ConfigureAwait(true);
            var trailing = this.FindTrailingPresenter(root);
            _ = trailing.Visibility.Should().Be(Visibility.Visible);

            this.tree.TrailingContentTemplateSelector = null;
            await RenderAsync().ConfigureAwait(true);

            _ = trailing.Visibility.Should().Be(Visibility.Collapsed);
        });

    [TestMethod]
    public Task TrailingContentAction_DoesNotSelectItsRow_Async() => EnqueueAsync(
        async () =>
        {
            var root = (TreeItemAdapter)this.viewModel!.ShownItems.First();
            this.tree!.TrailingContentWidth = 72;
            this.tree.TrailingContentTemplateSelector = new TestTrailingContentSelector(CreateTemplate(
                "<ToggleButton Content='Lock' IsChecked='{Binding IsLocked, Mode=TwoWay}' />"));
            await RenderAsync().ConfigureAwait(true);
            var lockAction = this.FindTrailingPresenter(root).FindDescendant<ToggleButton>()!;

            ((IToggleProvider)new ToggleButtonAutomationPeer(lockAction).GetPattern(PatternInterface.Toggle)).Toggle();

            _ = root.IsLocked.Should().BeTrue();
            _ = root.IsSelected.Should().BeFalse("an action in the optional trailing slot must not select its row");
        });

    [TestMethod]
    public Task ItemRowHeight_UpdatesRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var row = this.FindRow(this.viewModel!.ShownItems.First());

            this.tree!.ItemRowHeight = 44;
            await RenderAsync().ConfigureAwait(true);

            _ = row.ItemRowHeight.Should().Be(44);
            _ = FindContentGrid(row).MinHeight.Should().Be(44, "the row template binds the content's minimum height to the row height");
        });

    [TestMethod]
    public Task ItemFontSize_UpdatesRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var row = this.FindRow(this.viewModel!.ShownItems.First());

            this.tree!.ItemFontSize = 18;
            await RenderAsync().ConfigureAwait(true);

            _ = FindLabel(row).FontSize.Should().Be(18);
        });

    [TestMethod]
    public Task ItemIconSize_UpdatesRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var row = this.FindRow(this.viewModel!.ShownItems.First());

            this.tree!.ItemIconSize = 28;
            await RenderAsync().ConfigureAwait(true);

            _ = row.ItemIconSize.Should().Be(28);
            _ = FindExpanderHost(row).Width.Should().Be(28);
        });

    [TestMethod]
    public Task ItemIconMargin_UpdatesRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var row = this.FindRow(this.viewModel!.ShownItems.First());

            this.tree!.ItemIconMargin = new Thickness(2, 0, 2, 0);
            await RenderAsync().ConfigureAwait(true);

            _ = FindExpanderHost(row).Margin.Left.Should().Be(2);
        });

    [TestMethod]
    public Task ItemIndentWidth_UpdatesRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var child = (await root.Children.ConfigureAwait(true))[0];

            this.tree!.ItemIndentWidth = 18;
            await RenderAsync().ConfigureAwait(true);

            _ = FindContentGrid(this.FindRow(child)).Margin.Left.Should().Be(child.Depth * 18);
        });

    [TestMethod]
    public Task LongLabel_IsTrimmedWithoutWrappingAndShowsFullTextAsToolTip_Async() => EnqueueAsync(
        async () =>
        {
            var root = this.viewModel!.ShownItems.First();
            var fullLabel = new string('L', 200);

            root.Label = fullLabel;
            await RenderAsync().ConfigureAwait(true);

            var label = FindLabel(this.FindRow(root));
            _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
            _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = ToolTipService.GetToolTip(label).Should().Be(fullLabel);
        });

    [TestMethod]
    [DataRow(0d)]
    [DataRow(64d)]
    public Task TrailingActions_AlignAcrossDepth_Async(double width) => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var testTree = this.tree!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var child = (await root.Children.ConfigureAwait(true))[0];
        await vm.ExpandItemAsync(child).ConfigureAwait(true);
        testTree.Width = 300;
        testTree.TrailingContentWidth = width;
        testTree.TrailingContentTemplateSelector = new TestTrailingContentSelector(CreateTemplate("<Button Width='24' MinWidth='24' Height='24' MinHeight='24' Padding='0' VerticalAlignment='Center' HorizontalAlignment='Center' />"));
        await RenderAsync().ConfigureAwait(true);

        var actionPositions = vm.ShownItems
            .Select(item => this.FindTrailingPresenter(item).FindDescendant<Button>()!.TransformToVisual(testTree).TransformPoint(default).X)
            .ToArray();

        _ = actionPositions.Should().OnlyContain(position => Math.Abs(position - actionPositions[0]) <= 1);
    });

    [TestMethod]
    public Task GenericItemWithoutDisplayLabel_RendersItsLabelAndToolTip_Async() => EnqueueAsync(async () =>
    {
        var item = new Mock<ITreeItem>();
        _ = item.SetupAllProperties();
        _ = item.Setup(value => value.Equals(It.IsAny<ITreeItem>())).Returns((ITreeItem other) => ReferenceEquals(other, item.Object));
        item.Object.Label = "Generic item";
        _ = item.SetupGet(value => value.Children).Returns(Task.FromResult(new ReadOnlyObservableCollection<ITreeItem>([])));

        var row = new TreeItemControl { ItemAdapter = item.Object };
        await LoadTestContentAsync(row).ConfigureAwait(true);

        var label = FindLabel(row);
        _ = label.Text.Should().Be("Generic item");
        _ = ToolTipService.GetToolTip(label).Should().Be("Generic item");
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
                this.tree = new TestableDynamicTree() { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    private static Task RenderAsync() => TreeRenderHelpers.RenderAsync();

    private static (Popup Popup, TextBox Editor) GetRenameEditor(TreeItemControl row)
    {
        var popup = row.FindDescendant<Popup>(candidate => string.Equals(candidate.Name, TreeItemControl.InPlaceRenamePart, StringComparison.Ordinal))!;
        return (popup, ((StackPanel)popup.Child).Children.OfType<TextBox>().Single());
    }

    /// <summary>
    ///     Finds where text is drawn in <paramref name="element" /> from its rendered pixels: ink is any pixel that
    ///     stands out from the element's dominant background.
    /// </summary>
    /// <param name="element">The element to render.</param>
    /// <param name="reference">The element whose coordinates the bounds are given in.</param>
    /// <param name="bandWidth">The width, from the first ink column, measured for the top and bottom; null for all.</param>
    /// <returns>The ink bounds, in <paramref name="reference" /> coordinates.</returns>
    private static async Task<Rect> RenderedInkBoundsAsync(FrameworkElement element, UIElement reference, double? bandWidth)
    {
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(element);
        var pixels = (await bitmap.GetPixelsAsync()).ToArray();
        var width = bitmap.PixelWidth;
        var height = bitmap.PixelHeight;
        await SaveCaptureIfRequestedAsync(element, pixels, width, height).ConfigureAwait(true);
        var scale = width / element.ActualWidth;

        var alpha = new double[width * height];
        var luminance = new double[width * height];
        for (var index = 0; index < alpha.Length; index++)
        {
            var pixel = index * 4;
            alpha[index] = pixels[pixel + 3] / 255.0;
            luminance[index] = ((0.0722 * pixels[pixel]) + (0.7152 * pixels[pixel + 1]) + (0.2126 * pixels[pixel + 2])) / 255.0;
        }

        var backgroundAlpha = alpha.Order().ElementAt(alpha.Length / 2);
        var backgroundLuminance = luminance.Order().ElementAt(luminance.Length / 2);
        // On a transparent background (a label) every opaque pixel is ink; on an opaque one (a text box) ink is an
        // opaque pixel that contrasts with it, so the box's transparent rounded corners are not ink.
        bool IsInk(int x, int y)
            => alpha[(y * width) + x] >= 0.5
                && (backgroundAlpha < 0.5 || Math.Abs(luminance[(y * width) + x] - backgroundLuminance) > 0.35);

        var left = Enumerable.Range(0, width).First(x => Enumerable.Range(0, height).Any(y => IsInk(x, y)));
        var right = Enumerable.Range(left, width - left).Last(x => Enumerable.Range(0, height).Any(y => IsInk(x, y))) + 1;
        if (bandWidth is { } band)
        {
            right = Math.Min(width, left + (int)Math.Ceiling(band * scale));
        }

        var inkRows = Enumerable.Range(0, height).Where(y => Enumerable.Range(left, right - left).Any(x => IsInk(x, y))).ToList();
        var origin = element.TransformToVisual(reference).TransformPoint(default);
        return new Rect(
            origin.X + (left / scale),
            origin.Y + (inkRows[0] / scale),
            (right - left) / scale,
            (inkRows[^1] + 1 - inkRows[0]) / scale);
    }

    // Writes the rendered pixels as a PNG when DYNAMICTREE_UI_CAPTURE_DIRECTORY names a folder, to inspect a failure.
    private static async Task SaveCaptureIfRequestedAsync(FrameworkElement element, byte[] pixels, int width, int height)
    {
        var directory = Environment.GetEnvironmentVariable("DYNAMICTREE_UI_CAPTURE_DIRECTORY");
        if (string.IsNullOrWhiteSpace(directory))
        {
            return;
        }

        _ = Directory.CreateDirectory(directory);
        var name = $"{element.GetType().Name}-{element.Name}-{DateTime.Now:HHmmssfff}.png";
        var stream = File.Create(Path.Combine(directory, name));
        await using var streamLifetime = stream.ConfigureAwait(true);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream.AsRandomAccessStream());
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)width, (uint)height, 96, 96, pixels);
        await encoder.FlushAsync();
    }

    private static TextBlock FindLabel(TreeItemControl row)
        => row.FindDescendant<TextBlock>(text => string.Equals(text.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;

    private static Grid FindContentGrid(TreeItemControl row)
        => row.FindDescendant<Grid>(grid => string.Equals(grid.Name, TreeItemControl.ContentGridPart, StringComparison.Ordinal))!;

    private static Border FindExpanderHost(TreeItemControl row)
        => row.FindDescendant<Border>(border => border.Child is TreeExpander)!;

    private static DataTemplate CreateTemplate(string content)
        => (DataTemplate)XamlReader.Load($"<DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>{content}</DataTemplate>");

    private TreeItemControl FindRow(ITreeItem item)
        => this.tree!.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;

    private ContentPresenter FindTrailingPresenter(ITreeItem item)
        => this.FindRow(item).FindDescendant<ContentPresenter>(presenter => string.Equals(presenter.Name, TreeItemControl.TrailingContentPresenterPart, StringComparison.Ordinal))!;

    private sealed partial class TestTrailingContentSelector(DataTemplate template) : DataTemplateSelector
    {
        protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => template;
    }
}
