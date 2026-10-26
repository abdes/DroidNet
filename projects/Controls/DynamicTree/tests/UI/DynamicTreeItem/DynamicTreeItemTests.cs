// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Tests.DynamicTreeItem;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTreeItem")]
[TestCategory("UITest")]
public partial class DynamicTreeItemTests : VisualUserInterfaceTests
{
    private Controls.DynamicTreeItem? treeItem;
    private TestVisualStateManager? vsm;
    private TreeItemAdapterMock? itemAdapter;

    [TestMethod]
    public Task InitializesTemplateParts_Async() => EnqueueAsync(this.AssertTemplateParts);

    [TestMethod]
    public Task InitializesTemplatePartsOnItemAdapterChange_Async() => EnqueueAsync(
        async () =>
        {
            this.treeItem = new Controls.DynamicTreeItem(); // No ItemAdapter set
            await LoadTestContentAsync(this.treeItem).ConfigureAwait(true);

            this.treeItem.ItemAdapter = new TreeItemAdapterMock { Label = "Modified Item" };

            this.AssertTemplateParts();
        });

    [TestMethod]
    public Task ForwardsTheExpanderExpandEvent_Async() => EnqueueAsync(
        () =>
        {
            var eventFired = false;
            this.treeItem!.Expand += (_, _) => eventFired = true;

            this.FindExpander().Toggle();

            _ = eventFired.Should().BeTrue();
        });

    [TestMethod]
    public Task ForwardsTheExpanderCollapseEvent_Async() => EnqueueAsync(
        () =>
        {
            var expander = this.FindExpander();
            expander.IsExpanded = true;
            var eventFired = false;
            this.treeItem!.Collapse += (_, _) => eventFired = true;

            expander.Toggle();

            _ = eventFired.Should().BeTrue();
        });

    [TestMethod]
    public Task UpdatesVisualStateWhenSelected_Async() => EnqueueAsync(
        () =>
        {
            this.itemAdapter!.IsSelected = true;

            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain(Controls.DynamicTreeItem.SelectedVisualState);
        });

    [TestMethod]
    public Task UpdatesVisualStateWhenDeselected_Async() => EnqueueAsync(
        () =>
        {
            this.itemAdapter!.IsSelected = true;

            this.itemAdapter.IsSelected = false;

            // The state manager logs every state entered; the latest one is the current state.
            _ = this.vsm!.GetCurrentStates(this.treeItem)!.Last().Should().NotBe(Controls.DynamicTreeItem.SelectedVisualState);
        });

    [TestMethod]
    public Task UpdatesVisualState_ItemWithNoChildren_ShowsNoChildrenAndCollapsed_Async() => EnqueueAsync(
        () =>
        {
            // Act
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: false) { IsExpanded = false };

            // Assert
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain([Controls.DynamicTreeItem.NoChildrenVisualState, Controls.DynamicTreeItem.CollapsedVisualState]);
            _ = this.vsm.GetCurrentStates(this.treeItem).Should().NotContain([Controls.DynamicTreeItem.WithChildrenVisualState, Controls.DynamicTreeItem.ExpandedVisualState]);
        });

    [TestMethod]
    public Task UpdatesVisualState_WhenTheItemExpansionChanges_Async() => EnqueueAsync(
        () =>
        {
            // Setup
            var adapter = new TreeItemAdapterMock(withChildren: true) { IsExpanded = false };
            this.treeItem!.ItemAdapter = adapter;

            // Act
            adapter.IsExpanded = true;

            // Assert
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain(Controls.DynamicTreeItem.ExpandedVisualState);
            _ = this.FindExpander().IsExpanded.Should().BeTrue();
        });

    [TestMethod]
    public Task UpdatesVisualState_WhenItemWithChildrenCollapsed_Async() => EnqueueAsync(
        () =>
        {
            // Act
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: true) { IsExpanded = false };

            // Assert
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain([Controls.DynamicTreeItem.WithChildrenVisualState, Controls.DynamicTreeItem.CollapsedVisualState]);
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().NotContain([Controls.DynamicTreeItem.ExpandedVisualState]);
        });

    [TestMethod]
    public Task UpdatesVisualState_WhenItemWithChildrenExpanded_Async() => EnqueueAsync(
        () =>
        {
            // Act
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: true) { IsExpanded = true };

            // Assert
            _ = this.FindExpander().IsExpanded.Should().BeTrue();
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain([Controls.DynamicTreeItem.WithChildrenVisualState, Controls.DynamicTreeItem.ExpandedVisualState]);
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().NotContain([Controls.DynamicTreeItem.CollapsedVisualState]);
        });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
        async () =>
        {
            this.itemAdapter = new TreeItemAdapterMock { IsSelected = false };
            this.treeItem = new Controls.DynamicTreeItem() { ItemAdapter = this.itemAdapter };
            await LoadTestContentAsync(this.treeItem).ConfigureAwait(true);

            var vsmTarget = this.treeItem.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTreeItem.RootGridPart, StringComparison.Ordinal));
            _ = vsmTarget.Should().NotBeNull();
            this.vsm = new TestVisualStateManager();
            VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

            taskCompletionSource.SetResult();
        });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    private Controls.Expander FindExpander()
        => this.treeItem!.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, Controls.DynamicTreeItem.ExpanderPart, StringComparison.Ordinal))!;

    private void AssertTemplateParts()
    {
        var item = this.treeItem!;
        _ = item.FindDescendant<Grid>(e => string.Equals(e.Name, Controls.DynamicTreeItem.RootGridPart, StringComparison.Ordinal)).Should().NotBeNull();
        _ = item.FindDescendant<ContentPresenter>(e => string.Equals(e.Name, Controls.DynamicTreeItem.ContentPresenterPart, StringComparison.Ordinal)).Should().NotBeNull();
        _ = this.FindExpander().Should().NotBeNull();
        _ = item.FindDescendant<TextBlock>(e => string.Equals(e.Name, Controls.DynamicTreeItem.ItemNamePart, StringComparison.Ordinal)).Should().NotBeNull();
        _ = item.FindDescendant<Popup>(e => string.Equals(e.Name, Controls.DynamicTreeItem.InPlaceRenamePart, StringComparison.Ordinal)).Should().NotBeNull();
    }

    private sealed partial class TreeItemAdapterMock(bool withChildren = false) : TreeItemAdapter
    {
        private string label = "Test Item";

        public override string Label
        {
            get => this.label + (withChildren ? " with children" : string.Empty);
            set => this.SetProperty(ref this.label, value);
        }

        public override bool ValidateItemName(string name) => true;

        protected override int DoGetChildrenCount() => withChildren ? 1 : 0;

        protected override Task LoadChildren() => Task.CompletedTask;
    }
}
