// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tabs.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class TabStripItemTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(async () =>
    {
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Test Tab",
            Icon = new SymbolIconSource { Symbol = Symbol.Document },
            IsClosable = true,
            IsPinned = false,
            IsSelected = false,
        }).ConfigureAwait(true);

        // Assert - Verify all required template parts are present
        CheckPartIsThere(tabStripItem, TabStripItem.ContentRootGridPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.IconPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.HeaderPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.ButtonsContainerPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.PinButtonPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.CloseButtonPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.PinnedIndicatorPartName);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenDeselected_TransitionsToNormalVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Normal Tab",
            IsSelected = true,
        }).ConfigureAwait(true);

        // Act - Deselect the item to trigger transition to Normal state
        tabStripItem.Item!.IsSelected = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should be in normal state initially
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.NormalVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenSelected_TransitionsToSelectedVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Selected Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Act
        tabStripItem.Item!.IsSelected = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.SelectedVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenPointerOver_TransitionsToPointerOverVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "PointerOver Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Act - Simulate pointer over by calling OnPointerEntered
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.PointerOverVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenSelectedAndPointerOver_TransitionsToSelectedPointerOverVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "SelectedPointerOver Tab",
            IsSelected = true,
        }).ConfigureAwait(true);

        // Act - Simulate pointer over by calling OnPointerEntered
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.SelectedPointerOverVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenPointerExits_TransitionsOutOfPointerOverStates_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "PointerExit Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Act - Enter pointer over
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.PointerOverVisualState]);

        // Act - Exit pointer
        tabStripItem.OnPointerExited();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should be back to Normal
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.NormalVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenPinned_TransitionsToPinnedVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Pinned Tab",
            IsPinned = false,
        }).ConfigureAwait(true);

        // Act - Pin the item
        tabStripItem.Item!.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.PinnedVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenUnpinned_TransitionsToUnpinnedVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Unpinned Tab",
            IsPinned = true,
        }).ConfigureAwait(true);

        // Act - Unpin the item
        tabStripItem.Item!.IsPinned = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.UnpinnedVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task CombinedVisualStates_SelectedAndPinned_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Selected Pinned Tab",
            IsSelected = false,
            IsPinned = false,
        }).ConfigureAwait(true);

        // Act
        tabStripItem.Item!.IsSelected = true;
        tabStripItem.Item.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should be in Selected and Pinned states
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.SelectedVisualState, TabStripItem.PinnedVisualState]);
    });

    [TestMethod]
    public Task DoesNotRespondToOldItemPropertyChanges_AfterItemReplaced_Async() => EnqueueAsync(async () =>
    {
        // Arrange - initial item is not selected
        var oldItem = new TabItem
        {
            Header = "Old Tab",
            IsSelected = false,
        };

        var (tabStripItem, vsm) = await SetupTabStripItemWithData(oldItem).ConfigureAwait(true);

        // Act - replace the Item with a new TabItem
        var newItem = new TabItem
        {
            Header = "New Tab",
            IsSelected = false,
        };

        tabStripItem.Item = newItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Mutate the old item - since the control should have unsubscribed, this must not affect visual state
        oldItem.IsSelected = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - the control should not transition to Selected because it's bound to the new item
        _ = vsm.GetCurrentStates(tabStripItem).Should().NotContain([TabStripItem.SelectedVisualState]);
    });

    [TestMethod]
    public Task DisabledControl_IsNotInteractive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Disabled Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Disable the control
        tabStripItem.IsEnabled = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - simulate pointer enter
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStripItem.OnPointerExited();
        await WaitForRenderCompletion().ConfigureAwait(true);
        tabStripItem.OnPinClicked();
        await WaitForRenderCompletion().ConfigureAwait(true);

        var closeRequested = false;
        tabStripItem.CloseRequested += (s, e) => closeRequested = true;

        tabStripItem.OnCloseClicked();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - no visual state transitions, and no event raised
        _ = vsm.GetCurrentStates(tabStripItem).Should().BeEmpty();
        _ = closeRequested.Should().BeFalse();
    });

    [TestMethod]
    public Task HandlesIsCompactPropertyChange_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Compact Tab",
            IsClosable = true,
        }).ConfigureAwait(true);

        // Act - Set compact mode
        tabStripItem.IsCompact = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Buttons should be overlaid with correct alignment
        var buttonsContainer = tabStripItem.FindDescendant<StackPanel>(e => string.Equals(e.Name, TabStripItem.ButtonsContainerPartName, StringComparison.Ordinal));
        _ = buttonsContainer.Should().NotBeNull();
        _ = buttonsContainer!.HorizontalAlignment.Should().Be(HorizontalAlignment.Right);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenPointerOver_TransitionsToOverlayVisibleVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Overlay PointerOver Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Act - Simulate pointer over by calling OnPointerEntered after template is applied
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Overlay visual state should be active
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.OverlayVisibleVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenPointerExits_TransitionsToOverlayHiddenVisualState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Overlay PointerExit Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Act - Enter pointer over to make overlay visible first
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.OverlayVisibleVisualState]);

        // Act - Exit pointer
        tabStripItem.OnPointerExited();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Overlay should be hidden
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.OverlayHiddenVisualState]);
    });

    [TestCategory("VisualStates")]
    [TestMethod]
    public Task WhenSelectedAndPointerOver_OverlayVisibleAndSelectedPointerOverVisualStates_Async() => EnqueueAsync(async () =>
    {
        // Arrange - start selected
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Selected Overlay Tab",
            IsSelected = true,
        }).ConfigureAwait(true);

        // Act - Simulate pointer over
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should have both SelectedPointerOver and OverlayVisible states
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.SelectedPointerOverVisualState, TabStripItem.OverlayVisibleVisualState]);
    });

    [TestMethod]
    public Task RaisesCloseRequestedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var eventRaised = false;
        TabItem? closedItem = null;
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Closable Tab",
            IsClosable = true,
        }).ConfigureAwait(true);

        tabStripItem.CloseRequested += (s, e) =>
        {
            eventRaised = true;
            closedItem = e.Item;
        };

        // Act - Call OnCloseClicked directly
        tabStripItem.OnCloseClicked();

        // Assert
        _ = eventRaised.Should().BeTrue();
        _ = closedItem.Should().Be(tabStripItem.Item);
    });

    [TestMethod]
    public Task OnCloseClicked_WithNullItem_DoesNotRaiseEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var eventRaised = false;
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem { Header = "Test" }).ConfigureAwait(true);
        tabStripItem.Item = null;
        tabStripItem.CloseRequested += (s, e) => eventRaised = true;

        // Act
        tabStripItem.OnCloseClicked();

        // Assert - Event should not be raised
        _ = eventRaised.Should().BeFalse();
    });

    [TestMethod]
    public Task TogglesPinStateOnPinButtonClick_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Pinnable Tab",
            IsPinned = false,
        }).ConfigureAwait(true);

        // Act - Call OnPinClicked directly
        tabStripItem.OnPinClicked();

        // Assert
        _ = tabStripItem.Item!.IsPinned.Should().BeTrue();
    });

    [TestMethod]
    public Task OnPinClicked_WithNullItem_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem { Header = "Test" }).ConfigureAwait(true);
        tabStripItem.Item = null;

        // Act & Assert - Should not throw
        tabStripItem.OnPinClicked();
    });

    [TestMethod]
    public Task UpdatesPinGlyphWhenPinned_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Pin Glyph Tab",
            IsPinned = false,
        }).ConfigureAwait(true);

        var pinButton = tabStripItem.FindDescendant<Button>(e => string.Equals(e.Name, TabStripItem.PinButtonPartName, StringComparison.Ordinal));
        _ = pinButton.Should().NotBeNull();
        var fontIcon = pinButton!.Content as FontIcon;
        _ = fontIcon.Should().NotBeNull();

        // Assert initial glyph (unpinned)
        _ = fontIcon!.Glyph.Should().Be("\uE718");

        // Act - Pin
        tabStripItem.Item!.IsPinned = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert pinned glyph
        _ = fontIcon.Glyph.Should().Be("\uE77A");

        // Act - Unpin
        tabStripItem.Item!.IsPinned = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert unpinned glyph
        _ = fontIcon.Glyph.Should().Be("\uE718");
    });

    [TestMethod]
    public Task ShowsCloseButtonWhenClosable_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Closable Tab",
            IsClosable = true,
        }).ConfigureAwait(true);

        // Assert
        var closeButton = tabStripItem.FindDescendant<Button>(e => string.Equals(e.Name, TabStripItem.CloseButtonPartName, StringComparison.Ordinal));
        _ = closeButton.Should().NotBeNull();
        _ = closeButton!.Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    public Task HidesCloseButtonWhenNotClosable_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Not Closable Tab",
            IsClosable = false,
        }).ConfigureAwait(true);

        // Assert
        var closeButton = tabStripItem.FindDescendant<Button>(e => string.Equals(e.Name, TabStripItem.CloseButtonPartName, StringComparison.Ordinal));
        _ = closeButton.Should().NotBeNull();
        _ = closeButton!.Visibility.Should().Be(Visibility.Collapsed);
    });

    [TestMethod]
    public Task ShowsPinnedIndicatorWhenPinned_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Pinned Tab",
            IsPinned = true,
        }).ConfigureAwait(true);

        // Assert
        var pinnedIndicator = tabStripItem.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, TabStripItem.PinnedIndicatorPartName, StringComparison.Ordinal));
        _ = pinnedIndicator.Should().NotBeNull();
        _ = pinnedIndicator!.Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    public Task HidesPinnedIndicatorWhenNotPinned_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Unpinned Tab",
            IsPinned = false,
        }).ConfigureAwait(true);

        // Assert
        var pinnedIndicator = tabStripItem.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, TabStripItem.PinnedIndicatorPartName, StringComparison.Ordinal));
        _ = pinnedIndicator.Should().NotBeNull();
        _ = pinnedIndicator!.Visibility.Should().Be(Visibility.Collapsed);
    });

    [TestMethod]
    public Task HandlesItemPropertySetToNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabItem = new TabItem { Header = "Test Tab", IsSelected = true };
        var (tabStripItem, _) = await SetupTabStripItemWithData(tabItem).ConfigureAwait(true);

        // Act - Set Item to null
        tabStripItem.Item = null;

        // Assert - Should not throw, and visual states should update
        // Since Item is null, UpdateVisualStates should handle it
        _ = tabStripItem.Item.Should().BeNull();
    });

    [TestMethod]
    public Task ItemSetToNull_DisablesControl_Async() => EnqueueAsync(async () =>
    {
        // Arrange - start with a valid item
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem { Header = "Nullable Tab", IsSelected = false }).ConfigureAwait(true);

        // Precondition - control should be enabled when bound to an item
        _ = tabStripItem.IsEnabled.Should().BeTrue();

        // Act - set Item to null
        tabStripItem.Item = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - control should be disabled and visual states cleared
        _ = tabStripItem.IsEnabled.Should().BeFalse();

        // Act - restore a non-null item
        var restored = new TabItem { Header = "Restored Tab", IsSelected = false };
        tabStripItem.Item = restored;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - control should be enabled again and bound to the restored item
        _ = tabStripItem.IsEnabled.Should().BeTrue();
        _ = tabStripItem.Item.Should().Be(restored);
    });

    [TestMethod]
    public Task SetsLoggerFactoryCorrectly_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var tabStripItem = new TestableTabStripItem();
        var loggerFactory = LoggerFactory.Create(builder => builder.AddDebug());

        // Act
        tabStripItem.LoggerFactory = loggerFactory;

        // Assert
        _ = tabStripItem.LoggerFactory.Should().Be(loggerFactory);
    });

    [TestMethod]
    public Task RecycledContainer_PinnedIconBinding_Rebinds_Async() => EnqueueAsync(async () =>
    {
        // This test reproduces the recycling/order-of-operations issue where a
        // container is reused and the template binding for the icon can end up
        // invalid. We simulate reuse by setting Item=null and then restoring the
        // original TabItem. The test asserts the PartIcon is present and has a
        // non-null IconSource and visible when the item is pinned.
        var tabItem = new TabItem
        {
            Header = "Recycled Tab",
            Icon = new SymbolIconSource { Symbol = Symbol.Document },
            IsPinned = true,
        };

        var (tabStripItem, _) = await SetupTabStripItemWithData(tabItem).ConfigureAwait(true);

        // Ensure initial state: icon part exists and should be visible
        var icon = tabStripItem.FindDescendant<IconSourceElement>(e => string.Equals(e.Name, TabStripItem.IconPartName, StringComparison.Ordinal));
        _ = icon.Should().NotBeNull();
        _ = icon!.IconSource.Should().NotBeNull();
        _ = icon.Visibility.Should().Be(Visibility.Visible);

        // Simulate recycling: clear the Item (container reused by ItemsRepeater)
        tabStripItem.Item = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Now restore the same item - this is where pre-fix code could leave the
        // icon binding invalid and the icon not visible for pinned items.
        tabStripItem.Item = tabItem;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert the icon part is still present and bound/shown.
        icon = tabStripItem.FindDescendant<IconSourceElement>(e => string.Equals(e.Name, TabStripItem.IconPartName, StringComparison.Ordinal));
        _ = icon.Should().NotBeNull();
        _ = icon!.IconSource.Should().NotBeNull();
        _ = icon.Visibility.Should().Be(Visibility.Visible);
    });

    [TestMethod]
    public Task PinUnpin_Repeated_TogglesIconVisibility_Async() => EnqueueAsync(async () =>
    {
        // Arrange - item starts unpinned with an icon
        var tabItem = new TabItem
        {
            Header = "Toggle Pin Tab",
            Icon = new SymbolIconSource { Symbol = Symbol.Document },
            IsPinned = false,
        };

        var (tabStripItem, _) = await SetupTabStripItemWithData(tabItem).ConfigureAwait(true);

        var icon = tabStripItem.FindDescendant<IconSourceElement>(e => string.Equals(e.Name, TabStripItem.IconPartName, StringComparison.Ordinal));
        _ = icon.Should().NotBeNull();

        // Define a helper to assert icon visible
        async Task AssertIconVisible()
        {
            await WaitForRenderCompletion().ConfigureAwait(true);
            _ = icon!.IconSource.Should().NotBeNull();
            _ = icon.Visibility.Should().Be(Visibility.Visible);
        }

        // Pin -> Unpin -> Pin -> Unpin
        tabStripItem.Item!.IsPinned = true;
        await AssertIconVisible().ConfigureAwait(true);

        tabStripItem.Item.IsPinned = false;
        await AssertIconVisible().ConfigureAwait(true);

        tabStripItem.Item.IsPinned = true;
        await AssertIconVisible().ConfigureAwait(true);

        tabStripItem.Item.IsPinned = false;
        await AssertIconVisible().ConfigureAwait(true);
    });

    internal static async Task<(TestableTabStripItem item, TestVisualStateManager vsm)> SetupTabStripItemWithData(TabItem tabItem)
    {
        var tabStripItem = new TestableTabStripItem { Item = tabItem };

        // Ensure the control has a usable ILoggerFactory for tests. Use a lightweight
        // Microsoft.Extensions.Logging factory so CreateLogger(...) returns a real
        // ILogger instance. We keep it minimal to avoid noisy output in test runs.
        var loggerFactory = LoggerFactory.Create(builder => builder
            .SetMinimumLevel(LogLevel.Debug)
            .AddDebug());
        tabStripItem.LoggerFactory = loggerFactory;

        // Load the control
        await LoadTestContentAsync(tabStripItem).ConfigureAwait(true);

        // We can only install the custom VSM after loading the template, and
        // so, we need to trigger the desired visual state explicitly by setting
        // the right property to the right value after the control is loaded.
        var rootGrid = tabStripItem.FindDescendant<Grid>(e => string.Equals(e.Name, TabStripItem.RootGridPartName, StringComparison.Ordinal));
        _ = rootGrid.Should().NotBeNull();
        var vsm = new TestVisualStateManager();
        VisualStateManager.SetCustomVisualStateManager(rootGrid, vsm);

        return (tabStripItem, vsm);
    }

    protected static async Task WaitForRenderCompletion() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);

    protected static void CheckPartIsThere(TabStripItem tabStripItem, string partName)
    {
        var part = tabStripItem.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
        _ = part.Should().NotBeNull($"Template part '{partName}' should be present");
    }
}
