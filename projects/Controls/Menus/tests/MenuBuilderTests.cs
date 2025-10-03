// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
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
    public Task AddRadioMenuItem_FluentAPI_ShouldConfigureRadioGroup_Async() => EnqueueAsync(() =>
    {
        // Act
        _ = this.builder.AddRadioMenuItem("Light", "theme", isChecked: true);
        _ = this.builder.AddRadioMenuItem("Dark", "theme");

        // Assert
        var items = this.builder.MenuItems;
        _ = items.Should().HaveCount(2);

        var first = items[0];
        _ = first.RadioGroupId.Should().Be("theme");
        _ = first.IsChecked.Should().BeTrue();
        _ = first.HasSelectionState.Should().BeTrue();

        var second = items[1];
        _ = second.RadioGroupId.Should().Be("theme");
        _ = second.IsChecked.Should().BeFalse();
    });

    [TestMethod]
    public void AddSubmenu_ShouldMaterializeChildItems()
    {
        // Act
        _ = this.builder.AddSubmenu("File", submenu =>
        {
            _ = submenu.AddMenuItem("New");
            _ = submenu.AddMenuItem("Open");
        });

        // Assert
        var submenu = this.builder.MenuItems.Should().ContainSingle().Subject;
        var realizedChildren = submenu.SubItems.Should().BeOfType<List<MenuItemData>>().Subject;
        _ = realizedChildren.Should().HaveCount(2);
        _ = realizedChildren.Select(child => child.Text).Should().Contain(["New", "Open"]);
    }

    [TestMethod]
    public void Build_ShouldReuseMenuSourceInstances()
    {
        // Arrange
        _ = this.builder.AddMenuItem("Item");

        // Act
        var first = this.builder.Build();
        var second = this.builder.Build();

        // Assert
        _ = second.Should().BeSameAs(first);
        _ = first.Items.Should().HaveCount(1);
        _ = first.Services.Should().NotBeNull();
    }

    [TestMethod]
    public void Build_ShouldAssignUniqueHierarchicalIdentifiers()
    {
        // Arrange
        _ = this.builder.AddSubmenu("File", file =>
        {
            _ = file.AddMenuItem("New");
            _ = file.AddMenuItem("New");
            _ = file.AddSubmenu("Recent", recent => recent.AddMenuItem("Project"));
        });
        _ = this.builder.AddSubmenu("File", file => file.AddMenuItem("Open"));
        _ = this.builder.AddMenuItem("Exit");

        // Act
        var source = this.builder.Build();

        // Assert
        var rootItems = source.Items;
        _ = rootItems.Should().HaveCount(3);
        _ = rootItems[0].Id.Should().Be("FILE");
        _ = rootItems[1].Id.Should().Be("FILE_1");
        _ = rootItems[2].Id.Should().Be("EXIT");

        var firstChildren = rootItems[0].SubItems.ToList();
        _ = firstChildren.Should().HaveCount(3);
        _ = firstChildren[0].Id.Should().Be("FILE.NEW");
        _ = firstChildren[1].Id.Should().Be("FILE.NEW_1");
        _ = firstChildren[2].Id.Should().Be("FILE.RECENT");

        var recentChildren = firstChildren[2].SubItems.ToList();
        _ = recentChildren.Should().ContainSingle();
        _ = recentChildren[0].Id.Should().Be("FILE.RECENT.PROJECT");
    }

    [TestMethod]
    public void AddMenuItem_AfterBuild_ShouldUpdateMenuSourceItems()
    {
        // Arrange
        _ = this.builder.AddMenuItem("Initial");
        var source = this.builder.Build();

        // Act
        _ = this.builder.AddMenuItem("Later");

        // Assert
        _ = source.Items.Should().HaveCount(2);
        _ = source.Items[1].Text.Should().Be("Later");
    }

    [TestMethod]
    public void Build_ShouldAssignDefaultIdentifiersWhenTextMissing()
    {
        // Arrange
        _ = this.builder.AddMenuItem(string.Empty);
        _ = this.builder.AddMenuItem("   ");

        // Act
        var source = this.builder.Build();

        // Assert
        _ = source.Items.Should().HaveCount(2);
        _ = source.Items[0].Id.Should().Be("ITEM");
        _ = source.Items[1].Id.Should().Be("ITEM_1");
    }

    [TestMethod]
    public void Build_ShouldUpdateIdentifiersWhenItemTextChanges()
    {
        // Arrange
        _ = this.builder.AddMenuItem("Original");
        var source = this.builder.Build();
        var item = source.Items[0];
        _ = item.Id.Should().Be("ORIGINAL");

        // Act
        item.Text = "Renamed Item";
        var updatedSource = this.builder.Build();

        // Assert
        var updatedItem = updatedSource.Items[0];
        _ = updatedItem.Id.Should().Be("RENAMED_ITEM");
        _ = updatedSource.Services.GetLookup().Should().ContainKey("RENAMED_ITEM");
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
