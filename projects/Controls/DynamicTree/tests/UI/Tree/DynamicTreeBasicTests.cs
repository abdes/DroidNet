// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Markup;
using Moq;
using Windows.System;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;
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
    private TestVisualStateManager? vsm;
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
    public Task VerifyInitialTreeStructure_Async() =>
        EnqueueAsync(
        () =>
        {
            // Assert
            _ = this.viewModel.Should().NotBeNull();
            var shownItems = this.viewModel!.ShownItems;
            _ = shownItems.Should().ContainSingle();

            var rootItem = shownItems.FirstOrDefault();
            _ = rootItem.Should().NotBeNull();
            _ = rootItem!.Label.Should().Be("R");
            _ = rootItem.IsExpanded.Should().BeFalse();

            // Verify the tree control only shows one DynamicTreeItem
            var treeItemsCount = CountItemsShownInTree(this.tree!);
            _ = treeItemsCount.Should().Be(1);
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

    [TestMethod]
    public Task MoveItems_MultiSelectionPreserved_Async() => EnqueueAsync(
        async () =>
        {
            // Arrange
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.GetShownItemAt(0);
            await vm.ExpandItemAsync(root).ConfigureAwait(true);

            var firstChild = (TreeItemAdapter)vm.GetShownItemAt(1);
            var secondChild = (TreeItemAdapter)vm.GetShownItemAt(2);

            vm.ClearAndSelectItem(firstChild);
            vm.SelectItem(secondChild, RequestOrigin.PointerInput);

            // Act
            await vm.MoveItemsAsync([firstChild, secondChild], root, 0).ConfigureAwait(true);

            // Assert
            var selectedItems = vm.ShownItems.Where(item => item.IsSelected).ToList();
            _ = selectedItems.Should().HaveCount(2);
            _ = selectedItems[0].Should().BeSameAs(firstChild);
            _ = selectedItems[1].Should().BeSameAs(secondChild);
        });

    [TestMethod]
    public Task InlineRename_UsesAsyncOwnerAndKeepsDraftWhenRejected_Async() => EnqueueAsync(
        async () =>
        {
            using var vm = new TestViewModel();
            await vm.LoadTreeStructureAsync().ConfigureAwait(true);
            var testTree = new TestableDynamicTree { ViewModel = vm };
            var focusSink = new Button { Content = "Focus sink" };
            var host = new Grid();
            host.RowDefinitions.Add(new RowDefinition { Height = new GridLength(300) });
            host.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            host.Children.Add(testTree);
            Grid.SetRow(focusSink, 1);
            host.Children.Add(focusSink);
            await LoadTestContentAsync(host).ConfigureAwait(true);

            var item = (TreeItemAdapter)vm.ShownItems.First();
            _ = await testTree.BeginRenameAsync(item).ConfigureAwait(true);
            var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;
            var renamePopup = row.FindDescendant<Popup>(popup => string.Equals(popup.Name, TreeItemControl.InPlaceRenamePart, StringComparison.Ordinal))!;
            var edit = ((StackPanel)renamePopup.Child).Children.OfType<TextBox>().Single();
            _ = edit.Should().NotBeNull();
            edit.Text = "Rejected draft";

            var rejected = new TaskCompletionSource<TreeItemRenameResult>(TaskCreationOptions.RunContinuationsAsynchronously);
            var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            vm.PendingRename = rejected;
            vm.RenameCommitStarted = started;
            _ = focusSink.Focus(FocusState.Programmatic);
            await started.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = item.Label.Should().Be("R");
            renamePopup.IsOpen = false;

            rejected.SetResult(TreeItemRenameResult.Rejected("Rejected by the owning model."));
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = edit.Visibility.Should().Be(Visibility.Visible);
            _ = edit.Text.Should().Be("Rejected draft");
            _ = renamePopup.IsOpen.Should().BeTrue();

            _ = edit.Focus(FocusState.Programmatic).Should().BeTrue();

            // Retry the unchanged draft: successful commit must clear the prior validation state.
            _ = focusSink.Focus(FocusState.Programmatic);
            await WaitForItemLabelAsync(item, "Rejected draft").ConfigureAwait(true);

            _ = item.Label.Should().Be("Rejected draft");
            _ = edit.Visibility.Should().Be(Visibility.Collapsed);
            _ = vm.RenameCommitCount.Should().Be(2);
            var error = ((StackPanel)renamePopup.Child).Children.OfType<FontIcon>().Single();
            _ = error.Visibility.Should().Be(Visibility.Collapsed);
            _ = ToolTipService.GetToolTip(error).Should().BeNull();
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
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        var item = vm.ShownItems.First();
        var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;
        var pending = new TaskCompletionSource<TreeItemRenameResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        vm.PendingRename = pending;
        vm.RenameCommitStarted = started;
        _ = row.BeginRename().Should().BeTrue();
        var popup = row.FindDescendant<Popup>(part => string.Equals(part.Name, TreeItemControl.InPlaceRenamePart, StringComparison.Ordinal))!;
        var editor = ((StackPanel)popup.Child).Children.OfType<TextBox>().Single();
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

        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        await commit.ConfigureAwait(true);
        _ = popup.IsOpen.Should().BeTrue();
        _ = editor.Text.Should().Be("Current draft");
        _ = currentPending.TrySetResult(TreeItemRenameResult.Success);
        await currentCommit.ConfigureAwait(true);
    });

    [TestMethod]
    public Task DisplayedSelectionScope_UsesFilteredRowsForRangeAndSelectAll_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var children = await root.Children.ConfigureAwait(true);
            await vm.ExpandItemAsync(children[0]).ConfigureAwait(true);
            await vm.ExpandItemAsync(children[1]).ConfigureAwait(true);
            vm.FilterPredicate = item => item.Label.Contains("GC1", StringComparison.Ordinal);
            this.tree!.IsFilteringEnabled = true;
            this.tree.SelectionScope = TreeSelectionScope.DisplayedItems;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

            var displayed = ((IEnumerable<ITreeItem>)this.tree.DisplayedItems!).ToArray();
            _ = displayed.Select(item => item.Label).Should().Equal("R", "R-C1", "R-C1-GC1", "R-C2", "R-C2-GC1");
            _ = this.tree.InvokeItemPointerPressed((TreeItemAdapter)displayed[2], isControlDown: false, isShiftDown: false, leftButtonPressed: true).Should().BeTrue();
            _ = this.tree.InvokeItemPointerPressed((TreeItemAdapter)displayed[4], isControlDown: false, isShiftDown: true, leftButtonPressed: true).Should().BeTrue();
            _ = vm.ShownItems.Where(item => item.IsSelected).Select(item => item.Label).Should().Equal("R-C1-GC1", "R-C2", "R-C2-GC1");

            _ = await this.tree.InvokeHandleKeyDownAsync(VirtualKey.A, isControlDown: true).ConfigureAwait(true);
            _ = vm.ShownItems.Where(item => item.IsSelected).Select(item => item.Label).Should().Equal("R", "R-C1", "R-C1-GC1", "R-C2", "R-C2-GC1");
            _ = vm.ShownItems.Single(item => string.Equals(item.Label, "R-C1-GC2", StringComparison.Ordinal)).IsSelected.Should().BeFalse();
        });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DropCopy_DuplicatesSubtreeWithoutReplacingClipboard_Async(bool cutClipboard) => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems.First();
            await vm.ExpandItemAsync(root).ConfigureAwait(true);
            var source = (TreeItemAdapter)vm.GetShownItemAt(1);
            var destination = (TreeItemAdapter)vm.GetShownItemAt(2);
            await vm.ExpandItemAsync(source).ConfigureAwait(true);
            if (cutClipboard)
            {
                await vm.CutItemsAsync([source]).ConfigureAwait(true);
            }
            else
            {
                await vm.CopyItemAsync(source).ConfigureAwait(true);
            }

            var clipboard = vm.ClipboardItems.ToArray();

            var result = await vm.CommitDropAsync(
                new TreeDropRequest([source], destination, destination.ChildrenCount, TreeDropOperation.Copy)).ConfigureAwait(true);

            _ = result.Succeeded.Should().BeTrue();
            var clone = result.Items.Should().ContainSingle().Which;
            _ = clone.Should().NotBeSameAs(source);
            _ = clone.Parent.Should().BeSameAs(destination);
            _ = (await clone.Children.ConfigureAwait(true)).Select(child => child.Label).Should().Equal("R-C1-GC1", "R-C1-GC2");
            _ = vm.CurrentClipboardState.Should().Be(cutClipboard ? ClipboardState.Cut : ClipboardState.Copied);
            _ = vm.IsClipboardValid.Should().BeTrue();
            _ = source.IsCut.Should().Be(cutClipboard);
            _ = vm.ClipboardItems.Should().Equal(clipboard);
            await vm.PasteItemsAsync(root).ConfigureAwait(true);
            _ = vm.CurrentClipboardState.Should().Be(ClipboardState.Empty);
        });

    [TestMethod]
    public Task DropCopy_SecondRootVetoLeavesTreeSelectionAndClipboardUnchanged_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        vm.ClearAndSelectItem(children[0]);
        await vm.CutItemsAsync([children[0]]).ConfigureAwait(true);
        var approvals = 0;
        vm.ItemBeingAdded += (_, args) => args.Proceed = ++approvals < 2;
        var result = await vm.CommitDropAsync(new TreeDropRequest([children[0], children[1]], root, 3, TreeDropOperation.Copy)).ConfigureAwait(true);
        _ = result.Succeeded.Should().BeFalse();
        _ = root.ChildrenCount.Should().Be(3);
        _ = vm.SelectedItem.Should().BeSameAs(children[0]);
        _ = vm.IsClipboardValid.Should().BeTrue();
        _ = children[0].IsCut.Should().BeTrue();
    });

    [TestMethod]
    public Task DropMove_SameParentVetoReportsRejectionWithoutInvalidatingClipboard_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        await vm.CopyItemAsync(children[0]).ConfigureAwait(true);
        vm.ItemBeingMoved += (_, args) => args.Proceed = false;
        var result = await vm.CommitDropAsync(new TreeDropRequest([children[0]], root, 3, TreeDropOperation.Move)).ConfigureAwait(true);
        _ = result.Succeeded.Should().BeFalse();
        _ = children[0].Should().BeSameAs((await root.Children.ConfigureAwait(true))[0]);
        _ = vm.IsClipboardValid.Should().BeTrue();
    });

    [TestMethod]
    public Task DropMove_AncestorSelectionAndRedirectReturnsActualRoots_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var children = await root.Children.ConfigureAwait(true);
        var source = children[0];
        var actualParent = children[1];
        await vm.ExpandItemAsync(source).ConfigureAwait(true);
        var descendant = (await source.Children.ConfigureAwait(true))[0];
        vm.ItemBeingMoved += (_, args) => args.NewParent = actualParent;
        var result = await vm.CommitDropAsync(new TreeDropRequest([source, descendant], root, 0, TreeDropOperation.Move)).ConfigureAwait(true);
        _ = result.Succeeded.Should().BeTrue();
        _ = result.Items.Should().Equal(source);
        _ = source.Parent.Should().BeSameAs(actualParent);
        _ = descendant.Parent.Should().BeSameAs(source);
    });

    [TestMethod]
    public Task TrailingContentSelectorUpdatesRealizedRowsAndKeepsItsColumnAligned_Async() => EnqueueAsync(
        async () =>
        {
            var vm = this.viewModel!;
            var root = (TreeItemAdapter)vm.ShownItems.First();
            this.tree!.TrailingContentWidth = 72;
            this.tree.TrailingContentTemplateSelector = new TestTrailingContentSelector(CreateTemplate(
                "<ToggleButton MinHeight='46' Content='Lock' IsChecked='{Binding IsLocked, Mode=TwoWay}' />"));
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

            var row = this.tree.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, root))!;
            var trailing = row.FindDescendant<ContentPresenter>(presenter => string.Equals(presenter.Name, TreeItemControl.TrailingContentPresenterPart, StringComparison.Ordinal))!;
            _ = trailing.Visibility.Should().Be(Visibility.Visible);
            var lockAction = (ToggleButton)trailing.FindDescendant<ToggleButton>()!;
            _ = row.ActualHeight.Should().BeGreaterThan(32);
            _ = ((Grid)row.FindDescendant<Grid>(grid => string.Equals(grid.Name, TreeItemControl.RootGridPart, StringComparison.Ordinal))!).ColumnDefinitions[1].ActualWidth.Should().BeApproximately(72, 1);
            ((IToggleProvider)new ToggleButtonAutomationPeer(lockAction).GetPattern(PatternInterface.Toggle)).Toggle();
            _ = root.IsLocked.Should().BeTrue();
            _ = root.IsSelected.Should().BeFalse("an action in the optional trailing slot must not select its row");

            this.tree.TrailingContentTemplateSelector = null;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = trailing.Visibility.Should().Be(Visibility.Collapsed);
            _ = ((Grid)row.FindDescendant<Grid>(grid => string.Equals(grid.Name, TreeItemControl.RootGridPart, StringComparison.Ordinal))!).ColumnDefinitions[1].ActualWidth.Should().Be(0);
        });

    [TestMethod]
    public Task ItemDensityPropertiesUpdateAlreadyRealizedRows_Async() => EnqueueAsync(
        async () =>
        {
            var root = this.viewModel!.ShownItems.First();
            var testTree = this.tree!;
            var row = testTree.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, root))!;
            _ = row.Should().NotBeNull();

            var fullLabel = new string('L', 200);
            root.Label = fullLabel;
            await this.viewModel!.ExpandItemAsync(root).ConfigureAwait(true);
            var child = (await root.Children.ConfigureAwait(true))[0];
            testTree.ItemRowHeight = 44;
            testTree.ItemFontSize = 18;
            testTree.ItemIconSize = 28;
            testTree.ItemIndentWidth = 18;
            testTree.ItemIconMargin = new Thickness(2, 0, 2, 0);
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            var childRow = testTree.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, child))!;
            _ = childRow.Should().NotBeNull();

            _ = row.ActualHeight.Should().BeGreaterThanOrEqualTo(44);
            var label = row.FindDescendant<TextBlock>(text => string.Equals(text.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
            _ = label.FontSize.Should().Be(18);
            _ = label.TextWrapping.Should().Be(TextWrapping.NoWrap);
            _ = label.TextTrimming.Should().Be(TextTrimming.CharacterEllipsis);
            _ = label.IsTextTrimmed.Should().BeTrue();
            _ = ToolTipService.GetToolTip(label).Should().Be(fullLabel);
            _ = ((Border)row.FindDescendant<Border>(border => border.Child is TreeExpander)!).ActualWidth.Should().Be(28);
            _ = ((Border)row.FindDescendant<Border>(border => border.Child is TreeExpander)!).Margin.Left.Should().Be(2);
            _ = ((Grid)childRow.FindDescendant<Grid>(grid => string.Equals(grid.Name, TreeItemControl.ContentGridPart, StringComparison.Ordinal))!).Margin.Left
                .Should().Be(child.Depth * 18);

            testTree.ItemRowHeight = 28;
            testTree.ItemFontSize = 12;
            testTree.ItemIconSize = 18;
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
            _ = row.ActualHeight.Should().BeLessThan(44);
            _ = row.FindDescendant<TextBlock>(text => string.Equals(text.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!.FontSize.Should().Be(12);
        });

    [TestMethod]
    [DataRow(0d)]
    [DataRow(64d)]
    public Task TrailingActions_AlignAcrossDepthAndLeaveScrollbarGutter_Async(double width) => EnqueueAsync(async () =>
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
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        double? actionX = null;
        foreach (var item in vm.ShownItems)
        {
            var row = testTree.FindDescendant<TreeItemControl>(control => ReferenceEquals(control.ItemAdapter, item))!;
            var presenter = row.FindDescendant<ContentPresenter>(control => string.Equals(control.Name, TreeItemControl.TrailingContentPresenterPart, StringComparison.Ordinal))!;
            var action = presenter.FindDescendant<Button>()!;
            var position = action.TransformToVisual(testTree).TransformPoint(default);
            _ = position.X.Should().BeApproximately(actionX ?? position.X, 1);
            actionX = position.X;
            var local = action.TransformToVisual(row).TransformPoint(default);
            _ = (local.Y + (action.ActualHeight / 2)).Should().BeApproximately(row.ActualHeight / 2, 1);
            _ = (row.ActualWidth - local.X - action.ActualWidth).Should().BeGreaterThanOrEqualTo(20);
        }
    });

    [TestMethod]
    public Task GenericItemWithoutDisplayLabelRendersAndPreservesMultiSelectionOnPress_Async() => EnqueueAsync(async () =>
    {
        var item = new Mock<ITreeItem>();
        _ = item.SetupAllProperties();
        _ = item.Setup(value => value.Equals(It.IsAny<ITreeItem>())).Returns((ITreeItem other) => ReferenceEquals(other, item.Object));
        item.Object.Label = "Generic item";
        item.Object.IsSelected = true;
        _ = item.SetupGet(value => value.Children).Returns(Task.FromResult(new ReadOnlyObservableCollection<ITreeItem>([])));
        var row = new TreeItemControl { ItemAdapter = item.Object };
        await LoadTestContentAsync(row).ConfigureAwait(true);
        var label = row.FindDescendant<TextBlock>(part => string.Equals(part.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;
        _ = label.Text.Should().Be("Generic item");
        _ = ToolTipService.GetToolTip(label).Should().Be("Generic item");

        var genericRoot = new Mock<ITreeItem>();
        _ = genericRoot.SetupAllProperties();
        _ = genericRoot.Setup(value => value.Equals(It.IsAny<ITreeItem>())).Returns((ITreeItem other) => ReferenceEquals(other, genericRoot.Object));
        genericRoot.Object.Label = "Root";
        _ = genericRoot.SetupGet(value => value.IsRoot).Returns(value: true);
        genericRoot.Object.IsExpanded = true;
        _ = genericRoot.SetupGet(value => value.CanAcceptChildren).Returns(value: true);
        _ = genericRoot.SetupGet(value => value.Children).Returns(Task.FromResult(new ReadOnlyObservableCollection<ITreeItem>([item.Object])));
        _ = item.SetupGet(value => value.Parent).Returns(genericRoot.Object);
        _ = item.SetupGet(value => value.Depth).Returns(1);
        using var vm = new TestViewModel { SelectionMode = SelectionMode.Multiple };
        await vm.InitializeAsync(genericRoot.Object).ConfigureAwait(true);
        var sut = new TestableDynamicTree { ViewModel = vm };
        await LoadTestContentAsync(sut).ConfigureAwait(true);
        vm.ClearAndSelectItem(item.Object);
        vm.SelectItem(genericRoot.Object, RequestOrigin.Programmatic);
        var count = vm.SelectedItemsCount;
        _ = count.Should().Be(2);
        _ = sut.InvokeGenericItemPointerPressed(item.Object).Should().BeTrue();
        _ = vm.SelectedItemsCount.Should().Be(count);
        _ = item.Object.IsSelected.Should().BeTrue();
        _ = genericRoot.Object.IsSelected.Should().BeTrue();
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DropOperationsRespectExistingVirtualMutationGuards_Async(bool allowed) => EnqueueAsync(async () =>
    {
        using var vm = new GuardedViewModel { AllowMutation = allowed };
        await vm.LoadTreeStructureAsync().ConfigureAwait(true);
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var source = (await root.Children.ConfigureAwait(true))[0];
        var move = await vm.CommitDropAsync(new TreeDropRequest([source], root, 3, TreeDropOperation.Move)).ConfigureAwait(true);
        _ = move.Succeeded.Should().Be(allowed);
        var copy = await vm.CommitDropAsync(new TreeDropRequest([source], root, 3, TreeDropOperation.Copy)).ConfigureAwait(true);
        _ = copy.Succeeded.Should().Be(allowed);
        _ = vm.MoveGuardCalls.Should().Be(1);
        _ = vm.InsertGuardCalls.Should().Be(1);
    });

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DropCopy_LateGuardFailureCompensatesDespiteDeleteVeto_Async(bool throws) => EnqueueAsync(async () =>
    {
        using var vm = new GuardedViewModel { AllowMutation = true, RejectSecondInsertion = true, ThrowOnRejection = throws, SelectionMode = SelectionMode.Multiple };
        await vm.LoadTreeStructureAsync().ConfigureAwait(true);
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var originals = (await root.Children.ConfigureAwait(true)).ToArray();
        vm.ClearAndSelectItem(originals[0]);
        await vm.CutItemsAsync([originals[0]]).ConfigureAwait(true);
        var consumerItems = new HashSet<ITreeItem>(originals);
        vm.ItemAdded += (_, args) => consumerItems.Add(args.TreeItem);
        vm.ItemRemoved += (_, args) => consumerItems.Remove(args.TreeItem);
        vm.ItemBeingRemoved += (_, args) => args.Proceed = false;
        var request = new TreeDropRequest([originals[0], originals[1]], root, 3, TreeDropOperation.Copy);
        if (throws)
        {
            Func<Task> act = () => vm.CommitDropAsync(request);
            _ = await act.Should().ThrowExactlyAsync<InvalidOperationException>().ConfigureAwait(true);
        }
        else
        {
            _ = (await vm.CommitDropAsync(request).ConfigureAwait(true)).Succeeded.Should().BeFalse();
        }

        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(originals);
        _ = consumerItems.Should().BeEquivalentTo(originals);
        _ = vm.SelectedItem.Should().BeSameAs(originals[0]);
        _ = vm.IsClipboardValid.Should().BeTrue();
        _ = originals[0].IsCut.Should().BeTrue();
    });

    [TestMethod]
    public Task PasteCopy_RemovingRealizedCopyHandlesDetachedDepth_Async() => EnqueueAsync(async () =>
    {
        var vm = this.viewModel!;
        var control = this.tree!;
        var root = vm.ShownItems.First();
        await vm.ExpandItemAsync(root).ConfigureAwait(true);
        var originalChildren = (await root.Children.ConfigureAwait(true)).ToArray();
        await vm.CopyItemAsync(originalChildren[0]).ConfigureAwait(true);
        await vm.PasteItemsAsync(root).ConfigureAwait(true);
        var copy = (await root.Children.ConfigureAwait(true)).Single(item => !originalChildren.Contains(item));
        control.UpdateLayout();
        var row = control.FindDescendant<TreeItemControl>(item => ReferenceEquals(item.ItemAdapter, copy));
        _ = row.Should().NotBeNull("undo removes a pasted item while its row is still realized");

        // The demo's paste undo uses this exact removal operation.
        await vm.RemoveItemAsync(copy).ConfigureAwait(true);
        _ = copy.Depth.Should().BeNegative();
        _ = copy.Parent.Should().BeNull();
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(originalChildren);
        _ = vm.ShownItems.Should().NotContain(copy);
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

                var vsmTarget = this.tree.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTree.RootGridPart, StringComparison.Ordinal));
                _ = vsmTarget.Should().NotBeNull();
                this.vsm = new TestVisualStateManager();
                VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    /// <summary>
    /// Counts the number of children of a specific type in an ItemsRepeater within a given tree.
    /// </summary>
    /// <param name="tree">The DynamicTree control.</param>
    /// <returns>The count of children of the specified type.</returns>
    private static int CountItemsShownInTree(DynamicTree tree)
    {
        var itemsRepeater = tree.FindDescendant<ItemsRepeater>(e => string.Equals(e.Name, Controls.DynamicTree.ItemsRepeaterPart, StringComparison.Ordinal));
        _ = itemsRepeater.Should().NotBeNull();

        return itemsRepeater!.ItemsSourceView.Count;
    }

    private static async Task WaitForItemLabelAsync(TreeItemAdapter item, string label)
    {
        for (var attempt = 0; attempt < 10 && !string.Equals(item.Label, label, StringComparison.Ordinal); attempt++)
        {
            _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        }
    }

    private static DataTemplate CreateTemplate(string content)
        => (DataTemplate)XamlReader.Load($"<DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>{content}</DataTemplate>");

    private sealed partial class TestTrailingContentSelector(DataTemplate template) : DataTemplateSelector
    {
        protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => template;
    }

    private sealed partial class GuardedViewModel : TestViewModel
    {
        public bool AllowMutation { get; init; }

        public bool RejectSecondInsertion { get; init; }

        public bool ThrowOnRejection { get; init; }

        public int MoveGuardCalls { get; private set; }

        public int InsertGuardCalls { get; private set; }

        public override Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem parent, int index)
        {
            this.MoveGuardCalls++;
            return this.AllowMutation ? base.MoveItemsAsync(items, parent, index) : Task.CompletedTask;
        }

        public override Task InsertItemAsync(ITreeItem item, ITreeItem parent, int index)
        {
            this.InsertGuardCalls++;
            return this.RejectSecondInsertion && this.InsertGuardCalls == 2
                ? this.ThrowOnRejection ? throw new InvalidOperationException("Rejected by the owner") : Task.CompletedTask
                : this.AllowMutation ? base.InsertItemAsync(item, parent, index) : Task.CompletedTask;
        }
    }
}
