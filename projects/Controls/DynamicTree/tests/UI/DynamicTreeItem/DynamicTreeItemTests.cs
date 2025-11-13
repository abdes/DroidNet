// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
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
    public Task InitializesTemplateParts_Async() => EnqueueAsync(
        () =>
        {
            // Assert
            _ = this.treeItem!.FindDescendant<Grid>(e => string.Equals(e.Name, "PartRootGrid", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem!.FindDescendant<ContentPresenter>(e => string.Equals(e.Name, "PartContentPresenter", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem!.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, "PartExpander", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem!.FindDescendant<TextBlock>(e => string.Equals(e.Name, "PartItemName", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem!.FindDescendant<Popup>(e => string.Equals(e.Name, "PartInPlaceRename", StringComparison.Ordinal)).Should().NotBeNull();
        });

    [TestMethod]
    public Task InitializesTemplatePartsOnItemAdapterChange_Async() => EnqueueAsync(
        async () =>
        {
            // Setup
            this.treeItem = new Controls.DynamicTreeItem(); // No ItemAdapter set
            await LoadTestContentAsync(this.treeItem).ConfigureAwait(true);

            // Act
            this.treeItem.ItemAdapter = new TreeItemAdapterMock { Label = "Modified Item" };

            // Assert
            _ = this.treeItem.FindDescendant<Grid>(e => string.Equals(e.Name, "PartRootGrid", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem.FindDescendant<ContentPresenter>(e => string.Equals(e.Name, "PartContentPresenter", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, "PartExpander", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem.FindDescendant<TextBlock>(e => string.Equals(e.Name, "PartItemName", StringComparison.Ordinal)).Should().NotBeNull();
            _ = this.treeItem.FindDescendant<Popup>(e => string.Equals(e.Name, "PartInPlaceRename", StringComparison.Ordinal)).Should().NotBeNull();
        });

    [TestMethod]
    public Task FiresExpandEventWhenExpanded_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var eventFired = false;

        // Find the Expander
        var expander = this.treeItem!.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, "PartExpander", StringComparison.Ordinal));
        _ = expander.Should().NotBeNull();

        // Handle the treeItem's Expand event and set IsExpanded to true
        this.treeItem!.Expand += (_, _) =>
        {
            eventFired = true;
            expander!.IsExpanded = true; // Set IsExpanded to true to keep it expanded
        };

        // Act
        expander!.Toggle(); // This will trigger the Expand event

        // Assert
        _ = eventFired.Should().BeTrue();
        _ = expander.IsExpanded.Should().BeTrue(); // Verify that it is actually expanded
    });

    [TestMethod]
    public Task FiresCollapseEventWhenCollapsed_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var eventFired = false;

        // Find the Expander
        var expander = this.treeItem!.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, "PartExpander", StringComparison.Ordinal));
        _ = expander.Should().NotBeNull();

        // Initially expand it
        this.treeItem!.Expand += (_, _) => expander!.IsExpanded = true;
        expander!.Toggle(); // Expand

        // Handle the treeItem's Collapse event and set IsExpanded to false
        this.treeItem!.Collapse += (_, _) =>
        {
            eventFired = true;
            expander.IsExpanded = false; // Set IsExpanded to false to keep it collapsed
        };

        // Act
        expander.Toggle(); // Collapse

        // Assert
        _ = eventFired.Should().BeTrue();
        _ = expander.IsExpanded.Should().BeFalse(); // Verify that it is actually collapsed
    });

    [TestMethod]
    public Task UpdatesVisualStateWhenSelected_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        this.itemAdapter!.IsSelected = false;

        // Act
        this.itemAdapter!.IsSelected = true;

        // Assert
        _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain("Selected");
    });

    [TestMethod]
    public Task UpdatesVisualStateWhenDeselected_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        this.itemAdapter!.IsSelected = true;

        // Act
        this.itemAdapter!.IsSelected = false;

        // Assert
        _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain("Normal");
    });

    [TestMethod]
    public Task UpdatesVisualState_ItemWithNoChildren_AlwaysCollapsed_Async() => EnqueueAsync(
        () =>
        {
            // Setup
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: false) { IsExpanded = false };
            this.itemAdapter!.IsSelected = true;
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain("Collapsed");

            // Act
            this.treeItem.ItemAdapter.IsExpanded = true;

            // Assert
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().NotContain(["Expanded"]);
        });

    [TestMethod]
    public Task UpdatesVisualState_WhenItemWithChildrenCollapsed_Async() => EnqueueAsync(
        () =>
        {
            // Act
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: true) { IsExpanded = false };

            // Assert
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain(["WithChildren", "Collapsed"]);
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().NotContain(["Expanded"]);
        });

    [TestMethod]
    public Task UpdatesVisualState_WhenItemWithChildrenExpanded_Async() => EnqueueAsync(
        () =>
        {
            // Act
            this.treeItem!.ItemAdapter = new TreeItemAdapterMock(withChildren: true) { IsExpanded = true };

            // Assert
            var expander = this.treeItem!.FindDescendant<Controls.Expander>(e => string.Equals(e.Name, "PartExpander", StringComparison.Ordinal));
            _ = expander.Should().NotBeNull();
            _ = expander!.IsExpanded.Should().BeTrue();
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().Contain(["WithChildren", "Expanded"]);
            _ = this.vsm!.GetCurrentStates(this.treeItem).Should().NotContain(["Collapsed"]);
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

            var vsmTarget = this.treeItem.FindDescendant<Grid>(e => string.Equals(e.Name, "PartRootGrid", StringComparison.Ordinal));
            _ = vsmTarget.Should().NotBeNull();
            this.vsm = new TestVisualStateManager();
            VisualStateManager.SetCustomVisualStateManager(vsmTarget, this.vsm);

            taskCompletionSource.SetResult();
        });
        await taskCompletionSource.Task.ConfigureAwait(true);
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
