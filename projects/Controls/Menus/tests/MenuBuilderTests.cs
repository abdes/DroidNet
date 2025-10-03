// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Windows.Input;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("MenuBuilderTests")]
[TestCategory("UITest")]
public partial class MenuBuilderTests : VisualUserInterfaceTests
{
    private MenuBuilder builder = null!;

    [TestInitialize]
    public void Setup() => this.builder = new MenuBuilder();

    [TestMethod]
    public void Constructor_ShouldInitializeEmptyMenuItems()
    {
        // Act & Assert
        _ = this.builder.MenuItems.Should().BeEmpty();
        _ = this.builder.AllItems.Should().BeEmpty();
    }

    [TestMethod]
    public void Constructor_WithAssignIdsTrue_ShouldEnableIdAssignment()
    {
        // Arrange
        var builderWithIds = new MenuBuilder(assignIds: true);

        // Act
        _ = builderWithIds.AddMenuItem(new MenuItemData { Text = "Test" });

        // Assert
        _ = builderWithIds.MenuItems[0].Id.Should().Be("TEST");
    }

    [TestMethod]
    public void Constructor_WithAssignIdsFalse_ShouldDisableIdAssignment()
    {
        // Arrange
        var builderWithoutIds = new MenuBuilder(assignIds: false);

        // Act
        _ = builderWithoutIds.AddMenuItem(new MenuItemData { Text = "Test" });

        // Assert
        _ = builderWithoutIds.MenuItems[0].Id.Should().BeEmpty();
    }

    [TestMethod]
    public Task AddMenuItem_ShouldAddItemAndAssignId_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var menuItem = new MenuItemData { Text = "File & Edit" };

        // Act
        var result = this.builder.AddMenuItem(menuItem);

        // Assert
        _ = result.Should().BeSameAs(this.builder);
        _ = this.builder.MenuItems.Should().HaveCount(1);
        _ = menuItem.Id.Should().Be("FILE_&_EDIT");
    });

    [TestMethod]
    public void AddMenuItem_WithDuplicateText_ShouldCreateUniqueIds()
    {
        // Arrange & Act
        _ = this.builder
            .AddMenuItem(new MenuItemData { Text = "File" })
            .AddMenuItem(new MenuItemData { Text = "File" });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("FILE");
        _ = this.builder.MenuItems[1].Id.Should().Be("FILE_1");
    }

    [TestMethod]
    public void AddMenuItem_WithSubItems_ShouldAssignHierarchicalIds()
    {
        // Arrange
        var subItem = new MenuItemData { Text = "New" };
        var menuItem = new MenuItemData
        {
            Text = "File",
            SubItems = [subItem,],
        };

        // Act
        _ = this.builder.AddMenuItem(menuItem);

        // Assert
        _ = menuItem.Id.Should().Be("FILE");
        _ = subItem.Id.Should().Be("FILE.NEW");
    }

    [TestMethod]
    public void AddMenuItem_WithEmptyText_ShouldAssignDefaultId()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = string.Empty });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("ITEM");
    }

    [TestMethod]
    public void AddMenuItem_WithWhitespaceText_ShouldAssignDefaultId()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = "   " });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("ITEM");
    }

    [TestMethod]
    public void AddMenuItem_WithSpecialCharacters_ShouldNormalizeId()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = "File-Menu.Item" });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("FILE_MENU_ITEM");
    }

    [TestMethod]
    public void AddMenuItem_WithOnlySpecialCharacters_ShouldFallbackToDefaultId()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = "!@#$%^*()" });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("ITEM");
    }

    [TestMethod]
    public void AddMenuItem_WithMixedSpecialCharacters_ShouldKeepValidCharacters()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = "A!B@C#D$" });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("ABCD");
    }

    [TestMethod]
    public void AddMenuItem_WithAmperstandMnemonic_ShouldPreserveAmpersand()
    {
        // Act
        _ = this.builder.AddMenuItem(new MenuItemData { Text = "&File Menu" });

        // Assert
        _ = this.builder.MenuItems[0].Id.Should().Be("&FILE_MENU");
    }

    [TestMethod]
    public Task AddMenuItem_FluentAPI_ShouldCreateItemWithProperties_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var command = new TestCommand();
        var icon = new SymbolIconSource { Symbol = Symbol.Add };

        // Act
        _ = this.builder.AddMenuItem("New", command, icon, "Ctrl+N");

        // Assert
        var item = this.builder.MenuItems[0];
        _ = item.Text.Should().Be("New");
        _ = item.Command.Should().BeSameAs(command);
        _ = item.Icon.Should().BeSameAs(icon);
        _ = item.AcceleratorText.Should().Be("Ctrl+N");
    });

    [TestMethod]
    public void AddSubmenu_ShouldCreateSubmenuWithItems()
    {
        // Act
        _ = this.builder.AddSubmenu("File", submenu =>
        {
            _ = submenu.AddMenuItem("New");
            _ = submenu.AddMenuItem("Open");
        });

        // Assert
        var fileMenu = this.builder.MenuItems[0];
        _ = fileMenu.Text.Should().Be("File");
        var subItems = fileMenu.SubItems.ToList();
        _ = subItems.Should().HaveCount(2);
        _ = subItems[0].Text.Should().Be("New");
        _ = subItems[1].Text.Should().Be("Open");
    }

    [TestMethod]
    public void AddCheckableMenuItem_ShouldCreateCheckableItem()
    {
        // Act
        _ = this.builder.AddCheckableMenuItem("Word Wrap", isChecked: true);

        // Assert
        var item = this.builder.MenuItems[0];
        _ = item.Text.Should().Be("Word Wrap");
        _ = item.IsCheckable.Should().BeTrue();
        _ = item.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void AddRadioMenuItem_ShouldCreateRadioGroupItem()
    {
        // Act
        _ = this.builder.AddRadioMenuItem("Left", "alignment", isChecked: true);

        // Assert
        var item = this.builder.MenuItems[0];
        _ = item.Text.Should().Be("Left");
        _ = item.RadioGroupId.Should().Be("alignment");
        _ = item.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void AddSeparator_ShouldCreateSeparatorItem()
    {
        // Act
        _ = this.builder.AddSeparator();

        // Assert
        var item = this.builder.MenuItems[0];
        _ = item.IsSeparator.Should().BeTrue();
    }

    [TestMethod]
    public void HandleGroupSelection_ShouldToggleCheckableItem()
    {
        // Arrange
        _ = this.builder.AddCheckableMenuItem("Word Wrap", isChecked: false);
        var item = this.builder.MenuItems[0];

        // Act
        this.builder.HandleGroupSelection(item);

        // Assert
        _ = item.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void HandleGroupSelection_ShouldHandleRadioGroups()
    {
        // Arrange
        _ = this.builder
            .AddRadioMenuItem("Left", "alignment", isChecked: true)
            .AddRadioMenuItem("Center", "alignment", isChecked: false);

        var leftItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Left", StringComparison.Ordinal));
        var centerItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Center", StringComparison.Ordinal));

        // Act
        this.builder.HandleGroupSelection(centerItem);

        // Assert
        _ = leftItem.IsChecked.Should().BeFalse();
        _ = centerItem.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void HandleGroupSelection_WithMultipleRadioGroups_ShouldOnlyAffectCorrectGroup()
    {
        // Arrange
        _ = this.builder
            .AddRadioMenuItem("Left", "alignment", isChecked: true)
            .AddRadioMenuItem("Center", "alignment", isChecked: false)
            .AddRadioMenuItem("Light", "theme", isChecked: true)
            .AddRadioMenuItem("Dark", "theme", isChecked: false);

        var centerItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Center", StringComparison.Ordinal));
        var lightItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Light", StringComparison.Ordinal));
        var darkItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Dark", StringComparison.Ordinal));

        // Act
        this.builder.HandleGroupSelection(centerItem);

        // Assert - Only alignment group should be affected
        _ = centerItem.IsChecked.Should().BeTrue();
        _ = lightItem.IsChecked.Should().BeTrue(); // Should remain unchanged
        _ = darkItem.IsChecked.Should().BeFalse(); // Should remain unchanged
    }

    [TestMethod]
    public void HandleGroupSelection_WithNoItemsInGroup_ShouldNotThrow()
    {
        // Arrange
        var isolatedItem = new MenuItemData
        {
            Text = "Isolated",
            RadioGroupId = "nonexistent_group",
            IsChecked = false,
        };
        _ = this.builder.AddMenuItem(isolatedItem);

        // Act & Assert
        var act = () => this.builder.HandleGroupSelection(isolatedItem);
        _ = act.Should().NotThrow();
        _ = isolatedItem.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void HandleGroupSelection_WithSameItemSelectedTwice_ShouldRemainSelected()
    {
        // Arrange
        _ = this.builder.AddRadioMenuItem("Left", "alignment", isChecked: true);
        var leftItem = this.builder.AllItems.First(i => string.Equals(i.Text, "Left", StringComparison.Ordinal));

        // Act
        this.builder.HandleGroupSelection(leftItem);
        this.builder.HandleGroupSelection(leftItem);

        // Assert
        _ = leftItem.IsChecked.Should().BeTrue();
    }

    [TestMethod]
    public void HandleGroupSelection_WithCheckableItemInRadioGroup_ShouldDeselectOthers()
    {
        // Arrange
        _ = this.builder
            .AddRadioMenuItem("Option1", "test_group", isChecked: true)
            .AddRadioMenuItem("Option2", "test_group", isChecked: false)
            .AddRadioMenuItem("Option3", "test_group", isChecked: false);

        var option2 = this.builder.AllItems.First(i => string.Equals(i.Text, "Option2", StringComparison.Ordinal));
        var option1 = this.builder.AllItems.First(i => string.Equals(i.Text, "Option1", StringComparison.Ordinal));
        var option3 = this.builder.AllItems.First(i => string.Equals(i.Text, "Option3", StringComparison.Ordinal));

        // Act
        this.builder.HandleGroupSelection(option2);

        // Assert
        _ = option1.IsChecked.Should().BeFalse();
        _ = option2.IsChecked.Should().BeTrue();
        _ = option3.IsChecked.Should().BeFalse();
    }

    [TestMethod]
    public void TryGetMenuItemById_ShouldFindExistingItem()
    {
        // Arrange
        _ = this.builder.AddMenuItem("File");
        _ = this.builder.BuildMenuSystem(); // Triggers BuildLookup

        // Act
        var found = this.builder.TryGetMenuItemById("FILE", out var menuItem);

        // Assert
        _ = found.Should().BeTrue();
        _ = menuItem.Text.Should().Be("File");
    }

    [TestMethod]
    public Task TryGetMenuItemById_ShouldFindNestedItem_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder.AddSubmenu("File", submenu => submenu.AddMenuItem("New"));
        _ = this.builder.BuildMenuSystem();

        // Act
        var found = this.builder.TryGetMenuItemById("FILE.NEW", out var menuItem);

        // Assert
        _ = found.Should().BeTrue();
        _ = menuItem.Text.Should().Be("New");
    });

    [TestMethod]
    public void TryGetMenuItemById_WithNonExistentId_ShouldReturnFalse()
    {
        // Arrange
        _ = this.builder.AddMenuItem("File");
        _ = this.builder.BuildMenuSystem();

        // Act
        var found = this.builder.TryGetMenuItemById("NONEXISTENT", out var menuItem);

        // Assert
        _ = found.Should().BeFalse();
        _ = menuItem.Should().BeNull();
    }

    [TestMethod]
    public void TryGetMenuItemById_WithEmptyId_ShouldReturnFalse()
    {
        // Arrange
        _ = this.builder.AddMenuItem("File");
        _ = this.builder.BuildMenuSystem();

        // Act
        var found = this.builder.TryGetMenuItemById(string.Empty, out var menuItem);

        // Assert
        _ = found.Should().BeFalse();
        _ = menuItem.Should().BeNull();
    }

    [TestMethod]
    public void TryGetMenuItemById_BeforeBuildLookup_ShouldReturnFalse()
    {
        // Arrange
        _ = this.builder.AddMenuItem("File");

        // Act - Don't call BuildMenuSystem to build lookup
        var found = this.builder.TryGetMenuItemById("FILE", out var menuItem);

        // Assert
        _ = found.Should().BeFalse();
        _ = menuItem.Should().BeNull();
    }

    [TestMethod]
    public void AllItems_ShouldReturnFlattenedHierarchy()
    {
        // Arrange
        _ = this.builder
            .AddMenuItem("Root1")
            .AddSubmenu("Root2", submenu => submenu.AddMenuItem("Sub1"));

        // Act
        var allItems = this.builder.AllItems.ToList();

        // Assert
        _ = allItems.Should().HaveCount(3);
        _ = allItems.Select(i => i.Text).Should().Contain(["Root1", "Root2", "Sub1"]);
    }

    [TestMethod]
    public void AllItems_WithEmptyBuilder_ShouldReturnEmptyCollection()
    {
        // Act
        var allItems = this.builder.AllItems.ToList();

        // Assert
        _ = allItems.Should().BeEmpty();
    }

    [TestMethod]
    public void AllItems_WithDeepHierarchy_ShouldIncludeAllLevels()
    {
        // Arrange
        _ = this.builder.AddSubmenu("Level1", level1 =>
            level1.AddSubmenu("Level2", level2 =>
                level2.AddSubmenu("Level3", level3 =>
                    level3.AddMenuItem("DeepItem"))));

        // Act
        var allItems = this.builder.AllItems.ToList();

        // Assert
        _ = allItems.Should().HaveCount(4);
        _ = allItems.Select(i => i.Text).Should().Contain(["Level1", "Level2", "Level3", "DeepItem"]);
    }

    [TestMethod]
    public void AllItems_WithMultipleSubmenus_ShouldIncludeAllItems()
    {
        // Arrange
        _ = this.builder
            .AddSubmenu("File", file => file
                .AddMenuItem("New")
                .AddMenuItem("Open")
                .AddSubmenu("Recent", recent => recent
                    .AddMenuItem("File1.txt")
                    .AddMenuItem("File2.txt")))
            .AddSubmenu("Edit", edit => edit
                .AddMenuItem("Cut")
                .AddMenuItem("Copy"));

        // Act
        var allItems = this.builder.AllItems.ToList();

        // Assert
        _ = allItems.Should().HaveCount(9);
        _ = allItems.Select(i => i.Text).Should().Contain([
            "File", "New", "Open", "Recent", "File1.txt", "File2.txt", "Edit", "Cut", "Copy",
        ]);
    }

    [TestMethod]
    public void AllItems_AfterModification_ShouldReflectChanges()
    {
        // Arrange
        _ = this.builder.AddMenuItem("Initial");
        var initialCount = this.builder.AllItems.Count();

        // Act
        _ = this.builder.AddMenuItem("Additional");
        var newCount = this.builder.AllItems.Count();

        // Assert
        _ = initialCount.Should().Be(1);
        _ = newCount.Should().Be(2);
    }

    [TestMethod]
    public void AllItems_WithSeparators_ShouldIncludeSeparators()
    {
        // Arrange
        _ = this.builder
            .AddMenuItem("Item1")
            .AddSeparator()
            .AddMenuItem("Item2");

        // Act
        var allItems = this.builder.AllItems.ToList();

        // Assert
        _ = allItems.Should().HaveCount(3);
        _ = allItems[1].IsSeparator.Should().BeTrue();
    }

    [TestMethod]
    public void BuildMenuSystem_ShouldReturnObservableCollection()
    {
        // Arrange
        _ = this.builder.AddMenuItem("File").AddMenuItem("Edit");

        // Act
        var result = this.builder.BuildMenuSystem();

        // Assert
        _ = result.Should().BeOfType<ObservableCollection<MenuItemData>>();
        _ = result.Should().HaveCount(2);
    }

    [TestMethod]
    public Task BuildMenuFlyout_ShouldCreateMenuFlyout_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder.AddMenuItem("Copy").AddSeparator();

        // Act
        var result = this.builder.BuildMenuFlyout();

        // Assert
        _ = result.Should().BeOfType<MenuFlyout>();
        _ = result.Items.Should().HaveCount(2);
        _ = result.Items[1].Should().BeOfType<MenuFlyoutSeparator>();
    });

    [TestMethod]
    public Task BuildMenuBar_ShouldCreateMenuBar_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder.AddSubmenu("File", submenu => submenu.AddMenuItem("New"));

        // Act
        var result = this.builder.BuildMenuBar();

        // Assert
        _ = result.Should().BeOfType<MenuBar>();
        _ = result.Items.Should().HaveCount(1);
        _ = result.Items[0].Title.Should().Be("File");
    });

    [TestMethod]
    public Task BuildMenuFlyout_WithEmptyItems_ShouldCreateEmptyMenuFlyout_Async() => EnqueueAsync(() =>
    {
        // Act
        var result = this.builder.BuildMenuFlyout();

        // Assert
        _ = result.Should().BeOfType<MenuFlyout>();
        _ = result.Items.Should().BeEmpty();
    });

    [TestMethod]
    public Task BuildMenuBar_WithEmptyItems_ShouldCreateEmptyMenuBar_Async() => EnqueueAsync(() =>
    {
        // Act
        var result = this.builder.BuildMenuBar();

        // Assert
        _ = result.Should().BeOfType<MenuBar>();
        _ = result.Items.Should().BeEmpty();
    });

    [TestMethod]
    public void BuildMenuSystem_WithEmptyItems_ShouldCreateEmptyCollection()
    {
        // Act
        var result = this.builder.BuildMenuSystem();

        // Assert
        _ = result.Should().BeOfType<ObservableCollection<MenuItemData>>();
        _ = result.Should().BeEmpty();
    }

    [TestMethod]
    public Task BuildMenuFlyout_WithNestedSubmenus_ShouldCreateCorrectStructure_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder.AddSubmenu("Level1", level1 =>
            level1.AddSubmenu("Level2", level2 =>
                level2.AddMenuItem("DeepItem")));

        // Act
        var result = this.builder.BuildMenuFlyout();

        // Assert
        _ = result.Should().BeOfType<MenuFlyout>();
        _ = result.Items.Should().HaveCount(1);
        _ = result.Items[0].Should().BeOfType<MenuFlyoutSubItem>();

        var subItem = (MenuFlyoutSubItem)result.Items[0];
        _ = subItem.Items.Should().HaveCount(1);
        _ = subItem.Items[0].Should().BeOfType<MenuFlyoutSubItem>();
    });

    [TestMethod]
    public Task BuildMenuBar_WithItemsWithoutSubItems_ShouldCreateMenuBarItems_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder.AddMenuItem("SimpleItem");

        // Act
        var result = this.builder.BuildMenuBar();

        // Assert
        _ = result.Should().BeOfType<MenuBar>();
        _ = result.Items.Should().HaveCount(1);
        _ = result.Items[0].Title.Should().Be("SimpleItem");
        _ = result.Items[0].Items.Should().BeEmpty();
    });

    [TestMethod]
    public Task BuildMenuFlyout_WithMixedItems_ShouldHandleAllTypes_Async() => EnqueueAsync(() =>
    {
        // Arrange
        _ = this.builder
            .AddMenuItem("Regular Item")
            .AddSeparator()
            .AddCheckableMenuItem("Checkable", isChecked: true)
            .AddRadioMenuItem("Radio1", "group1", isChecked: true)
            .AddSubmenu("Submenu", sub => sub.AddMenuItem("SubItem"));

        // Act
        var result = this.builder.BuildMenuFlyout();

        // Assert
        _ = result.Should().BeOfType<MenuFlyout>();
        _ = result.Items.Should().HaveCount(5);
        _ = result.Items[0].Should().BeOfType<MenuFlyoutItem>();
        _ = result.Items[1].Should().BeOfType<MenuFlyoutSeparator>();
        _ = result.Items[2].Should().BeOfType<ToggleMenuFlyoutItem>();
        _ = result.Items[3].Should().BeOfType<ToggleMenuFlyoutItem>();
        _ = result.Items[4].Should().BeOfType<MenuFlyoutSubItem>();
    });

    [TestMethod]
    public void AddCheckableMenuItem_WithNullText_ShouldHandleGracefully()
    {
        // Act & Assert
        var act = () => this.builder.AddCheckableMenuItem(null!);
        _ = act.Should().NotThrow();
        _ = this.builder.MenuItems.Should().HaveCount(1);
        _ = this.builder.MenuItems[0].IsCheckable.Should().BeTrue();
    }

    [TestMethod]
    public void AddRadioMenuItem_WithNullText_ShouldHandleGracefully()
    {
        // Act & Assert
        var act = () => this.builder.AddRadioMenuItem(null!, "group");
        _ = act.Should().NotThrow();
        _ = this.builder.MenuItems.Should().HaveCount(1);
        _ = this.builder.MenuItems[0].RadioGroupId.Should().Be("group");
    }

    [TestMethod]
    public void AddRadioMenuItem_WithNullGroupId_ShouldHandleGracefully()
    {
        // Act & Assert
        var act = () => this.builder.AddRadioMenuItem("Test", null!);
        _ = act.Should().NotThrow();
        _ = this.builder.MenuItems.Should().HaveCount(1);
        _ = this.builder.MenuItems[0].RadioGroupId.Should().BeNull();
    }

    [TestMethod]
    public void FluentAPI_WithNullParameters_ShouldChainCorrectly()
    {
        // Act & Assert - All null parameters should be handled gracefully
        var act = () => this.builder
            .AddMenuItem("Test1", command: null, icon: null, acceleratorText: null)
            .AddCheckableMenuItem("Test2", command: null, icon: null)
            .AddRadioMenuItem("Test3", "group", command: null, icon: null);

        _ = act.Should().NotThrow();
        _ = this.builder.MenuItems.Should().HaveCount(3);
    }

    private sealed partial class TestCommand : ICommand
    {
        public event EventHandler? CanExecuteChanged
        {
            add { }
            remove { }
        }

        public bool CanExecute(object? parameter) => true;

        public void Execute(object? parameter)
        {
        }
    }
}
