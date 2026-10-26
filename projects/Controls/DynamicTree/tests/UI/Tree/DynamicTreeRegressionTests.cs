// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Windows.System;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;
using TreeItemControl = DroidNet.Controls.DynamicTreeItem;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// Rows are bound to their tree when realized, expose automation peers, the page keys move focus by a page of rows, and
/// the tree restores invalid layout values that reach it through XAML, bindings and styles.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed class DynamicTreeRegressionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task TreeAndRows_ExposeAutomationPeers_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 3, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 400, Height = 320 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);

        var treePeer = FrameworkElementAutomationPeer.CreatePeerForElement(tree);
        _ = treePeer.Should().BeOfType<DynamicTreeAutomationPeer>();
        _ = treePeer.GetAutomationControlType().Should().Be(AutomationControlType.Tree);

        var row = tree.FindDescendant<TreeItemControl>(value => ReferenceEquals(value.ItemAdapter, viewModel.GetShownItemAt(0)))!;
        var rowPeer = FrameworkElementAutomationPeer.CreatePeerForElement(row);
        _ = rowPeer.Should().BeOfType<DynamicTreeItemAutomationPeer>();
        _ = rowPeer.GetAutomationControlType().Should().Be(AutomationControlType.TreeItem);
        _ = rowPeer.GetName().Should().Be(row.ItemAdapter!.Label);
        _ = rowPeer.GetPattern(PatternInterface.ExpandCollapse).Should().NotBeNull();
        _ = rowPeer.GetPattern(PatternInterface.SelectionItem).Should().NotBeNull();
    });

    [TestMethod]
    public Task RealizedRows_AreAttachedToTheirTree_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 3, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 400, Height = 320, ItemIndentWidth = 20 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);

        var rows = TreeRenderHelpers.Rows(tree);

        _ = rows.Should().HaveCount(viewModel.ShownItemsCount);
        _ = rows.Should().OnlyContain(row => ReferenceEquals(row.OwnerTree, tree));
        _ = rows.Should().OnlyContain(row => row.ItemIndentWidth == 20, "the tree pushes its layout to the rows it realizes");
        _ = tree.GetRealizedRows().Should().HaveCount(rows.Count).And.OnlyContain(row => rows.Contains(row));
    });

    [TestMethod]
    public Task PageKeys_MoveFocusByMoreThanOneRowAndStopAtTheEnds_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 100, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new TestableDynamicTree { ViewModel = viewModel, Width = 400, Height = 320 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);
        var shown = viewModel.ShownItems.ToList();
        _ = viewModel.FocusItem(shown[0], RequestOrigin.KeyboardInput).Should().BeTrue();

        _ = (await tree.InvokeHandleKeyDownAsync(VirtualKey.PageDown).ConfigureAwait(true)).Should().BeTrue();
        var afterPageDown = shown.IndexOf(viewModel.FocusedItem!.Item);
        _ = afterPageDown.Should().BeGreaterThan(1, "a page is more than one row");
        _ = viewModel.FocusedItem.Origin.Should().Be(RequestOrigin.KeyboardInput);

        _ = (await tree.InvokeHandleKeyDownAsync(VirtualKey.PageUp).ConfigureAwait(true)).Should().BeTrue();
        _ = shown.IndexOf(viewModel.FocusedItem.Item).Should().Be(0, "a page up from the first page returns to the top");
        _ = (await tree.InvokeHandleKeyDownAsync(VirtualKey.PageUp).ConfigureAwait(true)).Should().BeFalse("the first row has no page above it");

        _ = viewModel.FocusItem(shown[^1], RequestOrigin.KeyboardInput).Should().BeTrue();
        _ = (await tree.InvokeHandleKeyDownAsync(VirtualKey.PageDown).ConfigureAwait(true)).Should().BeFalse("the last row has no page below it");
    });

    [TestMethod]
    public Task InvalidLayoutValuesFromTheDependencyPropertyPath_AreRestored_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 3, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 400, Height = 320, ItemRowHeight = 40 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);

        // XAML, bindings and styles set the dependency property directly, bypassing the CLR setter.
        tree.SetValue(DynamicTree.ItemRowHeightProperty, -1d);
        tree.SetValue(DynamicTree.ItemFontSizeProperty, double.NaN);
        tree.SetValue(DynamicTree.TrailingContentWidthProperty, -5d);

        _ = tree.ItemRowHeight.Should().Be(40);
        _ = tree.ItemFontSize.Should().Be(14);
        _ = tree.TrailingContentWidth.Should().Be(0);
    });

    [TestMethod]
    public Task RowExpanderAndRenameEditor_AreInteractiveSources_Async() => EnqueueAsync(async () =>
    {
        using var viewModel = new TestViewModel();
        var root = new LazyTreeItem("Root", 3, isRoot: true) { IsExpanded = true };
        await viewModel.InitializeAsync(root).ConfigureAwait(true);
        var tree = new DynamicTree { ViewModel = viewModel, Width = 400, Height = 320 };
        await LoadTestContentAsync(tree).ConfigureAwait(true);

        var row = tree.FindDescendant<TreeItemControl>()!;
        var expander = row.FindDescendant<Controls.Expander>()!;
        var label = row.FindDescendant<TextBlock>(item => string.Equals(item.Name, TreeItemControl.ItemNamePart, StringComparison.Ordinal))!;

        _ = row.IsInteractiveContentElement(expander).Should().BeTrue();
        _ = row.IsInteractiveContentElement(label).Should().BeFalse();
    });
}
