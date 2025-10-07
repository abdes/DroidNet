// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;

namespace DroidNet.Controls.Menus.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("MenuServicesTests")]
public partial class MenuServicesTests : VisualUserInterfaceTests
{
    [TestMethod]
    public void TryGetMenuItemById_ShouldReturnMenuItem()
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddSubmenu("File", file => file.AddMenuItem("New"));
        var services = builder.Build().Services;

        // Act
        var found = services.TryGetMenuItemById("FILE.NEW", out var menuItem);

        // Assert
        _ = found.Should().BeTrue();
        _ = menuItem.Should().NotBeNull();
        _ = menuItem!.Text.Should().Be("New");
    }

    [TestMethod]
    public void TryGetMenuItemById_ShouldReturnFalseWhenNotFound()
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddMenuItem("Item");
        var services = builder.Build().Services;

        // Act
        var found = services.TryGetMenuItemById("MISSING", out var menuItem);

        // Assert
        _ = found.Should().BeFalse();
        _ = menuItem.Should().BeNull();
    }

    [TestMethod]
    public void GetLookup_ShouldRefreshAfterBuilderUpdates()
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddMenuItem("First");
        var services = builder.Build().Services;

        // Act & Assert
        var initialLookup = services.GetLookup();
        _ = initialLookup.Should().ContainKey("FIRST");

        _ = builder.AddMenuItem("Second");
        var updatedLookup = services.GetLookup();
        _ = updatedLookup.Should().ContainKey("SECOND");
        _ = updatedLookup["SECOND"].Text.Should().Be("Second");
    }

    [TestMethod]
    public Task HandleGroupSelection_ShouldEnforceRadioGroups_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddRadioMenuItem("Light", "theme", isChecked: true);
        _ = builder.AddRadioMenuItem("Dark", "theme");
        var source = builder.Build();
        var services = source.Services;

        var light = source.Items[0];
        var dark = source.Items[1];

        // Act
        services.HandleGroupSelection(dark);

        // Assert
        _ = light.IsChecked.Should().BeFalse();
        _ = dark.IsChecked.Should().BeTrue();
    });

    [TestMethod]
    public Task HandleGroupSelection_ShouldToggleCheckableItem_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddCheckableMenuItem("Toggle", isChecked: false);
        var source = builder.Build();
        var services = source.Services;
        var item = source.Items[0];

        // Act & Assert
        services.HandleGroupSelection(item);
        _ = item.IsChecked.Should().BeTrue();

        services.HandleGroupSelection(item);
        _ = item.IsChecked.Should().BeFalse();
    });

    [TestMethod]
    public void TryGetMenuItemById_ShouldReturnFalseForBlankIdentifiers()
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddMenuItem("Item");
        var services = builder.Build().Services;

        // Act & Assert
        foreach (var identifier in new string?[] { null, string.Empty, "   " })
        {
            var found = services.TryGetMenuItemById(identifier!, out var menuItem);
            _ = found.Should().BeFalse($"Identifier '{identifier ?? "<null>"}' should not resolve a menu item.");
            _ = menuItem.Should().BeNull();
        }
    }

    [TestMethod]
    public Task HandleGroupSelection_ShouldNotToggleNonCheckableItems_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddMenuItem("Plain");
        var source = builder.Build();
        var services = source.Services;
        var item = source.Items[0];
        item.IsChecked = false;

        // Act
        services.HandleGroupSelection(item);

        // Assert
        _ = item.IsChecked.Should().BeFalse();
    });

    [TestMethod]
    public Task HandleGroupSelection_ShouldCoordinateNestedRadioGroup_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var builder = new MenuBuilder();
        _ = builder.AddSubmenu("View", submenu =>
        {
            _ = submenu.AddRadioMenuItem("Left", "layout", isChecked: true);
            _ = submenu.AddRadioMenuItem("Right", "layout");
        });
        var source = builder.Build();
        var services = source.Services;
        var submenu = source.Items[0];
        var children = submenu.SubItems.ToList();
        var left = children[0];
        var right = children[1];

        // Act
        services.HandleGroupSelection(right);

        // Assert
        _ = left.IsChecked.Should().BeFalse();
        _ = right.IsChecked.Should().BeTrue();
    });
}
