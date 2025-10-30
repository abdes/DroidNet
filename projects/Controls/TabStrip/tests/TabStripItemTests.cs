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
[TestCategory("TabStripItemTests")]
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
        CheckPartIsThere(tabStripItem, TabStripItem.RootGridPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.IconPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.HeaderPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.ButtonsContainerPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.PinButtonPartName);
        CheckPartIsThere(tabStripItem, TabStripItem.CloseButtonPartName);
    });

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
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain(["Pinned"]);
    });

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
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain(["Unpinned"]);
    });

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
        _ = vsm.GetCurrentStates(tabStripItem).Should().Contain([TabStripItem.SelectedVisualState, "Pinned"]);
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

        // Assert - Buttons should be overlaid (Grid.Column=2, HorizontalAlignment=Right)
        var buttonsContainer = tabStripItem.FindDescendant<StackPanel>(e => string.Equals(e.Name, TabStripItem.ButtonsContainerPartName, StringComparison.Ordinal));
        _ = buttonsContainer.Should().NotBeNull();
        _ = Grid.GetColumn(buttonsContainer!).Should().Be(2);
        _ = buttonsContainer!.HorizontalAlignment.Should().Be(HorizontalAlignment.Right);
    });

    [TestMethod]
    public Task ToolbarVisibilityInCompactMode_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (tabStripItem, _) = await SetupTabStripItemWithData(new TabItem
        {
            Header = "Compact Visibility Tab",
            IsClosable = true,
        }).ConfigureAwait(true);

        // Act - Set compact mode
        tabStripItem.IsCompact = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Buttons should be collapsed initially
        var buttonsContainer = tabStripItem.FindDescendant<StackPanel>(e => string.Equals(e.Name, TabStripItem.ButtonsContainerPartName, StringComparison.Ordinal));
        _ = buttonsContainer.Should().NotBeNull();
        _ = buttonsContainer!.Visibility.Should().Be(Visibility.Collapsed);

        // Act - Pointer enter
        tabStripItem.OnPointerEntered();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Buttons should be visible
        _ = buttonsContainer.Visibility.Should().Be(Visibility.Visible);

        // Act - Pointer exit
        tabStripItem.OnPointerExited();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Buttons should be collapsed again
        _ = buttonsContainer.Visibility.Should().Be(Visibility.Collapsed);
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
    public Task SetsLoggerFactoryCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStripItem = new TestableTabStripItem();
        var loggerFactory = LoggerFactory.Create(builder => builder.AddDebug());

        // Act
        tabStripItem.LoggerFactory = loggerFactory;

        // Assert
        _ = tabStripItem.LoggerFactory.Should().Be(loggerFactory);
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
        var vsmTarget = tabStripItem.FindDescendant<Grid>(e => string.Equals(e.Name, TabStripItem.RootGridPartName, StringComparison.Ordinal));
        _ = vsmTarget.Should().NotBeNull();
        var vsm = new TestVisualStateManager();
        VisualStateManager.SetCustomVisualStateManager(vsmTarget, vsm);

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
