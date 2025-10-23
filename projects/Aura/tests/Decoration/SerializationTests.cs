// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Decoration.Serialization;
using FluentAssertions;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Unit tests for JSON serialization of window decoration types.
/// </summary>
[TestClass]
[TestCategory("Serialization")]
public class SerializationTests
{
    private static readonly JsonSerializerOptions SharedOptions = new(WindowDecorationJsonContext.Default.Options);

    private readonly JsonSerializerOptions jsonOptions = SharedOptions;

    [TestMethod]
    public void WindowDecorationOptions_RoundTripSerialization_PreservesData()
    {
        // Arrange
        var original = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            TitleBar = new TitleBarOptions { Height = 40.0, ShowTitle = true },
            Buttons = new WindowButtonsOptions { ShowMaximize = false },
            Menu = new MenuOptions { MenuProviderId = "App.MainMenu", IsCompact = true },
            Backdrop = BackdropKind.MicaAlt,
            EnableSystemTitleBarOverlay = true,
        };

        // Act
        var json = JsonSerializer.Serialize(original, this.jsonOptions);
        var deserialized = JsonSerializer.Deserialize<WindowDecorationOptions>(json, this.jsonOptions);

        // Assert
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Category.Should().Be(original.Category);
        _ = deserialized.ChromeEnabled.Should().Be(original.ChromeEnabled);
        _ = deserialized.TitleBar.Height.Should().Be(original.TitleBar.Height);
        _ = deserialized.TitleBar.ShowTitle.Should().Be(original.TitleBar.ShowTitle);
        _ = deserialized.Buttons.ShowMaximize.Should().Be(original.Buttons.ShowMaximize);
        _ = deserialized.Menu.Should().NotBeNull();
        _ = deserialized.Menu!.MenuProviderId.Should().Be(original.Menu!.MenuProviderId);
        _ = deserialized.Menu.IsCompact.Should().Be(original.Menu.IsCompact);
        _ = deserialized.Backdrop.Should().Be(original.Backdrop);
        _ = deserialized.EnableSystemTitleBarOverlay.Should().Be(original.EnableSystemTitleBarOverlay);
    }

    [TestMethod]
    public void MenuOptions_SerializesOnlyProviderIdAndIsCompact()
    {
        // Arrange
        var menuOptions = new MenuOptions
        {
            MenuProviderId = "App.MainMenu",
            IsCompact = true,
        };

        // Act
        var json = JsonSerializer.Serialize(menuOptions, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"menuProviderId\"");
        _ = json.Should().Contain("\"App.MainMenu\"");
        _ = json.Should().Contain("\"isCompact\"");
        _ = json.Should().Contain("true");

        // Should not contain any menu source data
        _ = json.Should().NotContain("menuSource");
        _ = json.Should().NotContain("items");
    }

    [TestMethod]
    public void MenuOptions_DeserializesFromJson()
    {
        // Arrange
        const string json = """
            {
              "menuProviderId": "App.TestMenu",
              "isCompact": false
            }
            """;

        // Act
        var menuOptions = JsonSerializer.Deserialize<MenuOptions>(json, this.jsonOptions);

        // Assert
        _ = menuOptions.Should().NotBeNull();
        _ = menuOptions!.MenuProviderId.Should().Be("App.TestMenu");
        _ = menuOptions.IsCompact.Should().BeFalse();
    }

    [TestMethod]
    public void MenuOptions_RoundTripSerialization_PreservesData()
    {
        // Arrange
        var original = new MenuOptions
        {
            MenuProviderId = "App.ToolMenu",
            IsCompact = true,
        };

        // Act
        var json = JsonSerializer.Serialize(original, this.jsonOptions);
        var deserialized = JsonSerializer.Deserialize<MenuOptions>(json, this.jsonOptions);

        // Assert
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.MenuProviderId.Should().Be(original.MenuProviderId);
        _ = deserialized.IsCompact.Should().Be(original.IsCompact);
    }

    [TestMethod]
    public void MenuOptions_Deserialize_ThrowsOnMissingProviderId()
    {
        // Arrange
        const string json = """
            {
              "isCompact": true
            }
            """;

        // Act
        var act = () => JsonSerializer.Deserialize<MenuOptions>(json, this.jsonOptions);

        // Assert
        _ = act.Should().Throw<JsonException>()
            .WithMessage("*menuProviderId*");
    }

    [TestMethod]
    public void MenuOptions_Deserialize_ThrowsOnEmptyProviderId()
    {
        // Arrange
        const string json = """
            {
              "menuProviderId": "",
              "isCompact": false
            }
            """;

        // Act
        var act = () => JsonSerializer.Deserialize<MenuOptions>(json, this.jsonOptions);

        // Assert
        _ = act.Should().Throw<JsonException>()
            .WithMessage("*menuProviderId*");
    }

    [TestMethod]
    public void MenuOptions_Deserialize_HandlesNull()
    {
        // Arrange
        const string json = "null";

        // Act
        var result = JsonSerializer.Deserialize<MenuOptions>(json, this.jsonOptions);

        // Assert
        _ = result.Should().BeNull();
    }

    [TestMethod]
    public void BackdropKind_SerializesAsString()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = new("Test"),
            Backdrop = BackdropKind.MicaAlt,
        };

        // Act
        var json = JsonSerializer.Serialize(options, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"backdrop\"");
        _ = json.Should().Contain("\"MicaAlt\"");
        _ = json.Should().NotContain("\"backdrop\": 2");
    }

    [TestMethod]
    public void DragRegionBehavior_SerializesAsString()
    {
        // Arrange
        var titleBar = new TitleBarOptions
        {
            DragBehavior = DragRegionBehavior.Extended,
        };

        // Act
        var json = JsonSerializer.Serialize(titleBar, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"dragBehavior\"");
        _ = json.Should().Contain("\"Extended\"");
    }

    [TestMethod]
    public void ButtonPlacement_SerializesAsString()
    {
        // Arrange
        var buttons = new WindowButtonsOptions
        {
            Placement = ButtonPlacement.Left,
        };

        // Act
        var json = JsonSerializer.Serialize(buttons, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"placement\"");
        _ = json.Should().Contain("\"Left\"");
    }

    [TestMethod]
    public void NullProperties_AreOmittedFromJson()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = new("Test"),
            Menu = null,
        };

        // Act
        var json = JsonSerializer.Serialize(options, this.jsonOptions);

        // Assert
        _ = json.Should().NotContain("\"menu\"");
        _ = json.Should().NotContain("\"Menu\"");
    }

    [TestMethod]
    public void JsonOutput_IsIndented()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = new("Test"),
        };

        // Act
        var json = JsonSerializer.Serialize(options, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\n");
        _ = json.Should().Contain("  "); // Indentation
    }

    [TestMethod]
    public void PropertyNames_AreCamelCase()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = new("Test"),
            ChromeEnabled = true,
        };

        // Act
        var json = JsonSerializer.Serialize(options, this.jsonOptions);

        // Assert
        _ = json.Should().Contain("\"category\"");
        _ = json.Should().Contain("\"chromeEnabled\"");
        _ = json.Should().NotContain("\"Category\"");
        _ = json.Should().NotContain("\"ChromeEnabled\"");
    }

    [TestMethod]
    public void TitleBarOptions_RoundTripSerialization_PreservesData()
    {
        // Arrange
        var original = new TitleBarOptions
        {
            Height = 45.0,
            ShowTitle = false,
            ShowIcon = true,
            DragBehavior = DragRegionBehavior.Custom,
        };

        // Act
        var json = JsonSerializer.Serialize(original, this.jsonOptions);
        var deserialized = JsonSerializer.Deserialize<TitleBarOptions>(json, this.jsonOptions);

        // Assert
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Height.Should().Be(original.Height);
        _ = deserialized.ShowTitle.Should().Be(original.ShowTitle);
        _ = deserialized.ShowIcon.Should().Be(original.ShowIcon);
        _ = deserialized.DragBehavior.Should().Be(original.DragBehavior);
    }

    [TestMethod]
    public void WindowButtonsOptions_RoundTripSerialization_PreservesData()
    {
        // Arrange
        var original = new WindowButtonsOptions
        {
            ShowMinimize = false,
            ShowMaximize = true,
            ShowClose = true,
            Placement = ButtonPlacement.Auto,
        };

        // Act
        var json = JsonSerializer.Serialize(original, this.jsonOptions);
        var deserialized = JsonSerializer.Deserialize<WindowButtonsOptions>(json, this.jsonOptions);

        // Assert
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.ShowMinimize.Should().Be(original.ShowMinimize);
        _ = deserialized.ShowMaximize.Should().Be(original.ShowMaximize);
        _ = deserialized.ShowClose.Should().Be(original.ShowClose);
        _ = deserialized.Placement.Should().Be(original.Placement);
    }

    [TestMethod]
    public void Dictionary_OfDecorationOptions_CanBeSerialized()
    {
        // Arrange
        var dictionary = new Dictionary<WindowCategory, WindowDecorationOptions>()
        {
            [WindowCategory.Main] = WindowDecorationBuilder.ForMainWindow().Build(),
            [WindowCategory.Tool] = WindowDecorationBuilder.ForToolWindow().Build(),
        };

        // Act
        var json = JsonSerializer.Serialize(dictionary, this.jsonOptions);
        var deserialized = JsonSerializer.Deserialize<Dictionary<WindowCategory, WindowDecorationOptions>>(json, this.jsonOptions);

        // Assert
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Should().HaveCount(2);
        _ = deserialized[WindowCategory.Main].Category.Should().Be(WindowCategory.Main);
        _ = deserialized[WindowCategory.Tool].Category.Should().Be(WindowCategory.Tool);
    }
}
