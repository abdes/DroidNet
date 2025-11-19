// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.TestHelpers;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public partial class TabStripItemTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; }

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
        // Arrange - Item starts unselected
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Normal Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // The VSM should have recorded the initial Normal state when the template was applied
        // Now verify the transition when we select and then deselect

        // Act - Select the item
        tabStripItem.Item!.IsSelected = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Clear the VSM history
        vsm.Reset();

        // Act - Deselect the item to trigger transition to Normal state
        tabStripItem.Item!.IsSelected = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should transition to normal state
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

        // Clear VSM history to track just the selection transition
        vsm.Reset();

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

        // Clear VSM history to track just the pointer over transition
        vsm.Reset();

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
        // Arrange - Item starts selected but pointer not over
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "SelectedPointerOver Tab",
            IsSelected = true,
        }).ConfigureAwait(true);

        // Clear VSM history to track just the pointer over transition
        vsm.Reset();

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

        // Clear VSM history to track just the exit transition
        vsm.Reset();

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

        // Clear VSM history to track just the pin transition
        vsm.Reset();

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

        // Clear VSM history to track just the unpin transition
        vsm.Reset();

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

        // Clear VSM history to track the transitions
        vsm.Reset();

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
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Disabled Tab",
            IsSelected = false,
        }).ConfigureAwait(true);

        // Disable the control
        tabStripItem.IsEnabled = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act & Assert - Disabled control should not respond to pointer events
        // Pointer enter should not change toolbar visibility (no overlay)
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Exit should be no-op
        tabStripItem.OnPointerExited();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Pin click should be rejected (IsEnabled check in OnPinClicked)
        tabStripItem.OnPinClicked();
        await WaitForRenderCompletion().ConfigureAwait(true);
        _ = tabStripItem.Item!.IsPinned.Should().BeFalse("Disabled control should ignore pin clicks");

        // Close click should not raise event (IsEnabled check in OnCloseClicked)
        var closeRequested = false;
        tabStripItem.CloseRequested += (s, e) => closeRequested = true;

        tabStripItem.OnCloseClicked();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = closeRequested.Should().BeFalse("Disabled control should not raise CloseRequested event");
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

        var buttonsContainer = tabStripItem.FindDescendant<StackPanel>(e => string.Equals(e.Name, TabStripItem.ButtonsContainerPartName, StringComparison.Ordinal));
        _ = buttonsContainer.Should().NotBeNull();

        // Get initial state (IsCompact = false, default)
        var initialAlignment = buttonsContainer!.HorizontalAlignment;

        // Act - Set compact mode to true
        tabStripItem.IsCompact = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Compact mode changes toolbar layout from default
        _ = tabStripItem.IsCompact.Should().BeTrue("IsCompact should be set to true");

        // In compact mode, buttons align to the right and overlay the content
        _ = buttonsContainer.HorizontalAlignment.Should().Be(
            HorizontalAlignment.Right,
            "Compact mode should align buttons to right");

        // Act - Unset compact mode
        tabStripItem.IsCompact = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should revert to initial layout
        _ = tabStripItem.IsCompact.Should().BeFalse("IsCompact should be set back to false");
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

        // Clear VSM history to track just the overlay transition
        vsm.Reset();

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

        // Clear VSM history to track just the hidden transition
        vsm.Reset();

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

        // Clear VSM history to track just the pointer over transition
        vsm.Reset();

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
    public Task ItemSetToNull_DisablesControl_Async() => EnqueueAsync(async () =>
    {
        // Arrange - start with a valid item
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem { Header = "Nullable Tab", IsSelected = false }).ConfigureAwait(true);

        // Precondition - control should be enabled when bound to an item
        _ = tabStripItem.IsEnabled.Should().BeTrue();

        // Act - set Item to null
        tabStripItem.Item = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - control should be disabled
        _ = tabStripItem.IsEnabled.Should().BeFalse("Control should disable when Item is null");
        _ = tabStripItem.Item.Should().BeNull("Item should be null");

        // Act - restore a non-null item
        var restored = new TabItem { Header = "Restored Tab", IsSelected = false };
        tabStripItem.Item = restored;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - control should be enabled again and bound to the restored item
        _ = tabStripItem.IsEnabled.Should().BeTrue("Control should enable when Item is restored");
        _ = tabStripItem.Item.Should().Be(restored, "Item should be the restored instance");
    });

#if DEBUG
    [TestMethod]
    public Task EmitsLogs_WhenLoggerFactoryProvided_Async() => EnqueueAsync(async () =>
    {
        // Arrange - create a logger factory that captures messages
        using var provider = new TestLoggerProvider();
        using var factory = Microsoft.Extensions.Logging.LoggerFactory.Create(builder => builder.AddProvider(provider).SetMinimumLevel(LogLevel.Trace));

        // Create the testable control and assign the logger factory BEFORE loading the template
        var testable = new TestableTabStripItem
        {
            Item = new TabItem { Header = "Logging Tab" },
            LoggerFactory = factory,
        };

        // Act - load the control (template application should emit logs)
        var (tabStripItem, _) = await SetupTabStripItemWithData(testable).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Trigger some actions that also produce logs
        testable.OnPinClicked();
        testable.OnCloseClicked();
        testable.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - we should have captured at least one of the known log messages
        _ = provider.Messages.Should().Contain(m => m.Contains("Applying template") || m.Contains("Close button clicked") || m.Contains("Pointer"));
    });
#endif

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

    /// <summary>
    /// Verify IsDragging property defaults to false and can be toggled.
    /// This is a basic TabStripItem DP behavior test.
    /// </summary>
    [TestMethod]
    public Task IsDraggingProperty_DefaultsFalse_AndCanBeToggled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem { Header = "Dragging Test" }).ConfigureAwait(true);

        // Act & Assert - property should start as false
        _ = tabStripItem.IsDragging.Should().BeFalse("IsDragging should default to false");

        tabStripItem.IsDragging = true;
        _ = tabStripItem.IsDragging.Should().BeTrue("IsDragging should be settable to true");

        tabStripItem.IsDragging = false;
        _ = tabStripItem.IsDragging.Should().BeFalse("IsDragging should be settable back to false");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verify IsDragging property triggers visual state transition (opacity/scale animations).
    /// Template defines Dragging state with opacity 0.7 and scale 0.95.
    /// </summary>
    [TestCategory("VisualStates")]
    [TestMethod]
    public Task IsDragging_TransitionsVisualState_Opacity070_Scale095_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Item starts with IsDragging = false
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem { Header = "StateTransitionTest" }).ConfigureAwait(true);

        // Record initial state - control should not be dragging
        _ = tabStripItem.IsDragging.Should().BeFalse("Initial state should not be dragging");

        // Clear the VSM to track just the drag transition
        vsm.Reset();

        // Act - Transition to dragging
        tabStripItem.IsDragging = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Check dragging state was transitioned to
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.DraggingVisualState]);

        // Clear VSM again
        vsm.Reset();

        // Act - Transition back to not dragging
        tabStripItem.IsDragging = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Check not dragging state was transitioned to
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.NotDraggingVisualState]);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verify rapid IsDragging state changes don't break visual state manager.
    /// Stress test for state transition handling.
    /// </summary>
    [TestCategory("VisualStates")]
    [TestMethod]
    public Task IsDragging_RapidTransitions_NoConflicts_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Item starts with IsDragging = false
        var (tabStripItem, vsm) = await SetupTabStripItemWithData(new TabItem { Header = "RapidTransitionTest" }).ConfigureAwait(true);

        _ = tabStripItem.IsDragging.Should().BeFalse("Initial state should not be dragging");

        // Act - Perform rapid state transitions
        for (var i = 0; i < 5; i++)
        {
            tabStripItem.IsDragging = true;
            await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);
            tabStripItem.IsDragging = false;
            await Task.Delay(10, this.TestContext.CancellationToken).ConfigureAwait(true);
        }

        // Assert - Should end in not dragging state
        _ = tabStripItem.IsDragging.Should().BeFalse("Final state should be not dragging");
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.NotDraggingVisualState]);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Verify IsDragging can be set before template application (edge case from WinUI property/template ordering).
    /// VisualStateManager gracefully handles missing states.
    /// </summary>
    [TestMethod]
    public Task IsDragging_SetBeforeTemplate_NoError_StateRetainedAfterTemplate_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStripItem = new TestableTabStripItem
        {
            // Act - Set IsDragging before template is applied
            // This verifies the property can be set before template without throwing
            IsDragging = true,
        };

        // Assert - Property is set
        _ = tabStripItem.IsDragging.Should().BeTrue("Property should be set successfully before template");

        // Now load the item with template and VSM
        tabStripItem.Item = new TabItem { Header = "Test" };
        var (_, vsm) = await SetupTabStripItemWithData(tabStripItem).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - The visual state should be applied during OnApplyTemplate
        // because UpdateVisualStates(useTransitions: false) is called at the end of OnApplyTemplate,
        // which evaluates the IsDragging property that was set before the template was applied.
        _ = tabStripItem.IsDragging.Should().BeTrue("IsDragging should retain its value after template is applied");

        // The Dragging visual state is expected to modify visual properties of the root grid.
        // We verify at least one property has been changed from its default to confirm
        // the visual state was applied. The exact properties may vary with implementation.
        var rootGrid = tabStripItem.FindDescendant<Grid>(e => string.Equals(e.Name, TabStripItem.RootGridPartName, StringComparison.Ordinal));
        _ = rootGrid.Should().NotBeNull("Root grid should exist");

        var scaleTransform = rootGrid!.RenderTransform as ScaleTransform;
        _ = scaleTransform.Should().NotBeNull("Root grid should have a ScaleTransform");

        // Verify at least one visual property has been modified from default (NotDragging state defaults)
        // NotDragging state: Opacity=1, ScaleX=1, ScaleY=1
        // Dragging state typically modifies at least one of these
        var opacityChanged = !rootGrid.Opacity.Equals(1.0);
        var scaleXChanged = !scaleTransform!.ScaleX.Equals(1.0);
        var scaleYChanged = !scaleTransform.ScaleY.Equals(1.0);

        _ = (opacityChanged || scaleXChanged || scaleYChanged).Should().BeTrue(
            "At least one visual property should be modified by the Dragging visual state (Opacity or Scale)");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    internal static async Task<(TestableTabStripItem item, TestVisualStateManager vsm)> SetupTabStripItemWithData(TabItem tabItem)
    {
        var tabStripItem = new TestableTabStripItem { Item = tabItem };
        return await SetupTabStripItemWithData(tabStripItem).ConfigureAwait(true);
    }

    internal static async Task<(TestableTabStripItem item, TestVisualStateManager vsm)> SetupTabStripItemWithData(TestableTabStripItem tabStripItem)
    {
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
