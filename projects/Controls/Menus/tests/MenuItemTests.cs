// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("MenuItemTests")]
[TestCategory("UITest")]
public class MenuItemTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(async () =>
    {
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Symbol.Accept },
            AcceleratorText = "Ctrl+T",
            IsEnabled = true,
        }).ConfigureAwait(true);

        // Assert - Verify all required template parts are present
        CheckPartIsThere(menuItem, MenuItem.RootGridPart);
        CheckPartIsThere(menuItem, MenuItem.ContentGridPart);
        CheckPartIsThere(menuItem, MenuItem.IconPresenterPart);
        CheckPartIsThere(menuItem, MenuItem.TextBlockPart);
        CheckPartIsThere(menuItem, MenuItem.AcceleratorTextBlockPart);
        CheckPartIsThere(menuItem, MenuItem.SeparatorBorderPart);
    });

    [TestMethod]
    public Task DoesNotExecuteCommandWhenDisabled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Symbol.Accept },
            AcceleratorText = "Ctrl+T",
            IsEnabled = false,
            Command = new RelayCommand<MenuItemData>(_ => commandExecuted = true),
        }).ConfigureAwait(true);

        // Act - Try to execute command on disabled item
        menuItem.InvokeTapped();

        // Assert
        _ = commandExecuted.Should().BeFalse("Command should not execute when item is disabled");
    });

    [TestMethod]
    public Task DoesNotShowSelectionIndicatorWhenNotSelectable_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Non-selectable Item",
            Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Symbol.Accept },
            IsCheckable = false,
            RadioGroupId = null, // Not part of any radio group
            IsChecked = false,
        }).ConfigureAwait(true);
        _ = menuItem.ItemData.Should().NotBeNull();

        // Act
        menuItem.ItemData.IsChecked = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NoDecorationVisualState]);
        CheckPartIsAbsentOrCollapsed(menuItem, MenuItem.CheckmarkPart);
        CheckPartIsAbsentOrCollapsed(menuItem, MenuItem.StateTextBlockPart);
    });

    [TestMethod]
    public Task ExecutesCommandWhenEnabled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var commandParameter = (MenuItemData?)null;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            IsEnabled = true,
            Command = new RelayCommand<MenuItemData>(param =>
            {
                commandExecuted = true;
                commandParameter = param;
            }),
        }).ConfigureAwait(true);

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = commandExecuted.Should().BeTrue("Command should execute when item is enabled");
        _ = commandParameter.Should().Be(menuItem.ItemData, "Command parameter should be the menu item data");
    });

    [TestMethod]
    public Task ShowsCorrectVisualStateForNormalItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with disabled item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            IsEnabled = false,
        }).ConfigureAwait(true);

        // Act - Enable the item to trigger state change
        menuItem.ItemData!.IsEnabled = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NormalVisualState]);
    });

    [TestMethod]
    public Task ShowsCorrectVisualStateForDisabledItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with enabled item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            IsEnabled = true,
        }).ConfigureAwait(true);

        // Act - Disable the item to trigger state change
        menuItem.ItemData!.IsEnabled = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.DisabledVisualState]);
    });

    [TestMethod]
    public Task ShowsCorrectVisualStateForSeparator_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with normal item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Normal Item",
            IsSeparator = false,
        }).ConfigureAwait(true);

        // Act - Change to separator to trigger state change
        menuItem.ItemData!.IsSeparator = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.SeparatorVisualState]);
        CheckPartIsThere(menuItem, MenuItem.SeparatorBorderPart);
    });

    [TestMethod]
    public Task ShowsIconWhenIconIsSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start without icon
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            Icon = null,
        }).ConfigureAwait(true);

        // Act - Set icon to trigger state change
        menuItem.ItemData!.Icon = new SymbolIconSource { Symbol = Symbol.Save };
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.HasIconVisualState]);
        CheckPartIsThere(menuItem, MenuItem.IconPresenterPart);
    });

    [TestMethod]
    public Task HidesIconWhenNoIconIsSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with icon
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            Icon = new Microsoft.UI.Xaml.Controls.SymbolIconSource { Symbol = Symbol.Save },
        }).ConfigureAwait(true);

        // Act - Remove icon to trigger state change
        menuItem.ItemData!.Icon = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NoIconVisualState]);
    });

    [TestMethod]
    public Task ShowsAcceleratorTextWhenSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start without accelerator text
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Save",
            AcceleratorText = null,
        }).ConfigureAwait(true);

        // Act - Set accelerator text to trigger state change
        menuItem.ItemData!.AcceleratorText = "Ctrl+S";
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.HasAcceleratorVisualState]);
        CheckPartIsThere(menuItem, MenuItem.AcceleratorTextBlockPart);
    });

    [TestMethod]
    public Task HidesAcceleratorTextWhenNotSet_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with accelerator text
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            AcceleratorText = "Ctrl+S",
        }).ConfigureAwait(true);

        // Act - Remove accelerator text to trigger state change
        menuItem.ItemData!.AcceleratorText = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NoAcceleratorVisualState]);
    });

    [TestMethod]
    public Task ShowsSelectedStateForCheckableItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with unselected checkable item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable Item",
            IsCheckable = true,
            IsChecked = false,
        }).ConfigureAwait(true);

        // Act - Select the item to trigger state change
        menuItem.ItemData!.IsChecked = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.CheckedNoIconVisualState]);
        CheckPartIsThere(menuItem, MenuItem.CheckmarkPart);
    });

    [TestMethod]
    public Task ShowsUnselectedStateForUnselectedCheckableItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with selected checkable item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable Item",
            IsCheckable = true,
            IsChecked = true,
        }).ConfigureAwait(true);

        // Act - Unselect the item to trigger state change
        menuItem.ItemData!.IsChecked = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NoDecorationVisualState]);
    });

    [TestMethod]
    public Task ShowsSelectedStateForCheckableItemWithIcon_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with unselected checkable item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable Item",
            IsCheckable = true,
            IsChecked = false,
            Icon = new SymbolIconSource { Symbol = Symbol.Save },
        }).ConfigureAwait(true);

        // Act - Select the item to trigger state change
        menuItem.ItemData!.IsChecked = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.CheckedWithIconVisualState]);
        CheckPartIsThere(menuItem, MenuItem.CheckmarkPart);
    });

    [TestMethod]
    public Task ShowsUnselectedStateForUnselectedCheckableItemWithIcon_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with selected checkable item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable Item",
            IsCheckable = true,
            IsChecked = true,
            Icon = new SymbolIconSource { Symbol = Symbol.Save },
        }).ConfigureAwait(true);

        // Act - Unselect the item to trigger state change
        menuItem.ItemData!.IsChecked = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NoDecorationVisualState]);
    });

    [TestMethod]
    public Task TogglesCheckableItemOnInvoke_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Toggle Item",
            IsCheckable = true,
            IsChecked = false,
            IsEnabled = true,
        }).ConfigureAwait(true);

        var initialState = menuItem.ItemData!.IsChecked;

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = menuItem.ItemData.IsChecked.Should().Be(!initialState, "Checkable item should toggle its checked state");
    });

    [TestMethod]
    public Task HandlesRadioGroupSelection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var radioGroupEventRaised = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Radio Item",
            RadioGroupId = "TestGroup",
            IsChecked = false,
            IsEnabled = true,

            // No command needed - selection should work independently
        }).ConfigureAwait(true);

        menuItem.RadioGroupSelectionRequested += (_, _) => radioGroupEventRaised = true;

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = radioGroupEventRaised.Should().BeTrue("Radio group selection event should be raised for radio group items even without a command");
    });

    [TestMethod]
    public Task RaisesInvokedEventOnActivation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var invokedEventRaised = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            IsEnabled = true,
            Command = new RelayCommand<MenuItemData>(_ => { }),
        }).ConfigureAwait(true);

        menuItem.Invoked += (_, _) => invokedEventRaised = true;

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = invokedEventRaised.Should().BeTrue("Invoked event should be raised when menu item is activated");
    });

    [TestMethod]
    public Task UpdatesVisualStateOnItemDataPropertyChange_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start with disabled item
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Dynamic Item",
            IsEnabled = false,
        }).ConfigureAwait(true);

        // Act - Enable to trigger first state change
        menuItem.ItemData!.IsEnabled = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Verify the enabled state was captured
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.NormalVisualState]);

        // Act - Disable again to trigger second state change
        menuItem.ItemData!.IsEnabled = false;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Visual state should update to disabled
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.DisabledVisualState]);
    });

    [TestMethod]
    public Task HandlesSubmenuItems_Async() => EnqueueAsync(async () =>
    {
        // Arrange - Start without subitems
        var submenuRequestRaised = false;
        var (menuItem, vsm) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Parent Item",
            SubItems = [],
            IsEnabled = true,
        }).ConfigureAwait(true);

        menuItem.SubmenuRequested += (_, _) => submenuRequestRaised = true;

        // Act - Add subitems to trigger state change
        var subItems = new List<MenuItemData>
        {
            new() { Text = "Sub Item 1" },
            new() { Text = "Sub Item 2" },
        };
        menuItem.ItemData!.SubItems = subItems;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Check visual state
        _ = vsm.GetCurrentStates(menuItem).Should().Contain([MenuItem.WithChildrenVisualState]);

        // Test submenu invocation
        menuItem.InvokeTapped();
        _ = submenuRequestRaised.Should().BeTrue("Submenu request event should be raised for items with children");
    });

    [TestMethod]
    public Task DoesNotExecuteCommandForSeparator_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            IsSeparator = true,
            Command = new RelayCommand<MenuItemData>(_ => commandExecuted = true),
        }).ConfigureAwait(true);

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = commandExecuted.Should().BeFalse("Command should not execute for separator items");
    });

    [TestMethod]
    public Task HandlesCheckableItemWithoutCommand_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable Item",
            IsCheckable = true,
            IsChecked = false,
            IsEnabled = true,

            // Explicitly no command - selection should still work
        }).ConfigureAwait(true);

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = menuItem.ItemData!.IsChecked.Should().BeTrue("Checkable item should toggle its checked state even without a command");
    });

    [TestMethod]
    public Task DoesNotRaiseInvokedEventIfNoCommand_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var invokedEventRaised = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Test Item",
            IsEnabled = true,

            // No command provided
        }).ConfigureAwait(true);

        menuItem.Invoked += (_, _) => invokedEventRaised = true;

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = invokedEventRaised.Should().BeFalse("Invoked event should not be raised even when no command is present");
    });

    [TestMethod]
    public Task ExecutesCommandAndHandlesSelectionState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Checkable with Command",
            IsCheckable = true,
            IsChecked = false,
            IsEnabled = true,
            Command = new RelayCommand<MenuItemData>(_ => commandExecuted = true),
        }).ConfigureAwait(true);

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = menuItem.ItemData!.IsChecked.Should().BeTrue("Checked state should be handled");
        _ = commandExecuted.Should().BeTrue("Command should also be executed");
    });

    [TestMethod]
    public Task HandlesDisabledCommand_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var invokedEventRaised = false;
        const bool canExecute = false; // Control whether command can execute
        var command = new RelayCommand<MenuItemData>(
            _ => commandExecuted = true,
            _ => canExecute);

        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Item with Disabled Command",
            IsCheckable = true,
            IsChecked = false,
            IsEnabled = true,
            Command = command,
        }).ConfigureAwait(true);

        menuItem.Invoked += (_, _) => invokedEventRaised = true;

        // Act - Command is already disabled (canExecute = false)
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = commandExecuted.Should().BeFalse("Disabled command should not execute");
        _ = menuItem.ItemData!.IsChecked.Should().BeFalse("Checked state is updated only if the command successfully executes");
        _ = invokedEventRaised.Should().BeFalse("Invoked event should not be raised");
    });

    [TestMethod]
    public Task HandlesCanExecuteThrowing_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandExecuted = false;
        var commandFailedRaised = false;

        var command = new RelayCommand<MenuItemData>(
            _ => commandExecuted = true,
            _ => throw new InvalidOperationException("CanExecute failure"));

        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Item with Bad CanExecute",
            IsEnabled = true,
            IsCheckable = true,
            IsChecked = false,
            Command = command,
        }).ConfigureAwait(true);

        menuItem.CommandExecutionFailed += (_, _) => commandFailedRaised = true;

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - CanExecute threw so command should not execute and failure event should be raised
        _ = commandExecuted.Should().BeFalse("Command should not execute when CanExecute throws");
        _ = commandFailedRaised.Should().BeTrue("CommandExecutionFailed should be raised when CanExecute throws");
        _ = menuItem.ItemData!.IsChecked.Should().BeFalse("Checked state should not change when command cannot execute");
    });

    [TestMethod]
    public Task HandlesExecuteThrowing_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var commandFailedRaised = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Item with Bad Execute",
            IsEnabled = true,
            IsCheckable = true,
            IsChecked = false,
            Command = new RelayCommand<MenuItemData>(_ => throw new InvalidOperationException("Execute failure"), _ => true),
        }).ConfigureAwait(true);

        menuItem.CommandExecutionFailed += (_, _) => commandFailedRaised = true;

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Execute threw, command failure should be reported and checked state should not be toggled
        _ = commandFailedRaised.Should().BeTrue("CommandExecutionFailed should be raised when Execute throws");
        _ = menuItem.ItemData!.IsChecked.Should().BeFalse("Checked state should not change when Execute throws");
    });

    [TestMethod]
    public Task HandlesRadioGroupWithoutCommand_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var radioGroupEventRaised = false;
        var invokedEventRaised = false;
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Radio Item",
            RadioGroupId = "TestGroup",
            IsChecked = false,
            IsEnabled = true,

            // No command - should still handle radio group selection
        }).ConfigureAwait(true);

        menuItem.RadioGroupSelectionRequested += (_, _) => radioGroupEventRaised = true;
        menuItem.Invoked += (_, _) => invokedEventRaised = true;

        // Act
        menuItem.InvokeTapped();

        // Assert
        _ = radioGroupEventRaised.Should().BeTrue("Radio group selection should work without command");
        _ = invokedEventRaised.Should().BeFalse("Invoked event should not be raised");
    });

    [TestMethod]
    public Task DoesNotHandleSelectionStateForNonSelectableItems_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var (menuItem, _) = await SetupMenuItemWithData(new MenuItemData
        {
            Text = "Normal Item",
            IsCheckable = false,
            RadioGroupId = null,
            IsChecked = false,
            IsEnabled = true,
        }).ConfigureAwait(true);

        // Act
        menuItem.InvokeTapped();
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = menuItem.ItemData!.IsChecked.Should().BeFalse("Non-checkable item should not change checked state");
    });

    [TestMethod]
    public void CorrectlyIdentifiesHasSelectionState()
    {
        // Test checkable item
        var checkableItem = new MenuItemData
        {
            Text = "Checkable",
            IsCheckable = true,
        };
        _ = checkableItem.HasSelectionState.Should().BeTrue("Checkable items should have selection state");

        // Test radio group item
        var radioItem = new MenuItemData
        {
            Text = "Radio Item",
            RadioGroupId = "Group1",
        };
        _ = radioItem.HasSelectionState.Should().BeTrue("Radio group items should have selection state");

        // Test normal item
        var normalItem = new MenuItemData
        {
            Text = "Normal Item",
        };
        _ = normalItem.HasSelectionState.Should().BeFalse("Normal items should not have selection state");
    }

    [TestMethod]
    public void CorrectlyIdentifiesItemWithChildren()
    {
        // Test item with children
        var parentItem = new MenuItemData
        {
            Text = "Parent",
            SubItems = [new MenuItemData { Text = "Child" }],
        };
        _ = parentItem.HasChildren.Should().BeTrue("Item with sub-items should report HasChildren as true");
        _ = parentItem.IsLeafItem.Should().BeFalse("Item with sub-items should not be a leaf item");

        // Test item without children
        var leafItem = new MenuItemData
        {
            Text = "Leaf",
            SubItems = [],
        };
        _ = leafItem.HasChildren.Should().BeFalse("Item without sub-items should report HasChildren as false");
        _ = leafItem.IsLeafItem.Should().BeTrue("Item without sub-items should be a leaf item");
    }

    internal static async Task<(TestableMenuItem item, TestVisualStateManager vsm)>
        SetupMenuItemWithData(MenuItemData itemData)
    {
        // Create the MenuItem control with the provided data and load it as
        // the manin cointent for the test window.
        var menuItem = new TestableMenuItem { ItemData = itemData };
        await LoadTestContentAsync(menuItem).ConfigureAwait(true);

        // We can only install the custom VSM after loading the template, and
        // so, we need to trigger the desired visual state explicitly by setting
        // the right property to the right value after the control is loaded.
        var vsmTarget = menuItem.FindDescendant<Grid>(e => string.Equals(e.Name, MenuItem.RootGridPart, StringComparison.Ordinal));
        _ = vsmTarget.Should().NotBeNull();
        var vsm = new TestVisualStateManager();
        VisualStateManager.SetCustomVisualStateManager(vsmTarget, vsm);
        return (menuItem, vsm);
    }

    protected static async Task WaitForRenderCompletion() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);

    protected static void CheckPartIsThere(MenuItem menuItem, string partName)
    {
        var part = menuItem.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
        _ = part.Should().NotBeNull($"{partName} should be there");
    }

    protected static void CheckPartIsAbsentOrCollapsed(MenuItem menuItem, string partName)
    {
        var part = menuItem.FindDescendant<FrameworkElement>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
        if (part is not null)
        {
            _ = part.Visibility.Should().Be(
                Visibility.Collapsed,
                $"{partName} should not be there or should be collapsed");
        }
    }
}
