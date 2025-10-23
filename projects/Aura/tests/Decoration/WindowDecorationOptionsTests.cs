// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Decoration;
using FluentAssertions;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Test suite for <see cref="WindowDecorationOptions"/> validation and configuration.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class WindowDecorationOptionsTests
{
    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithValidOptions_ShouldNotThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default,
            Buttons = WindowButtonsOptions.Default,
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithMenuAndChromeDisabled_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            ChromeEnabled = false,
            Menu = new MenuOptions { MenuProviderId = "App.MainMenu" },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Menu cannot be specified when ChromeEnabled is false*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_PrimaryWindowWithoutCloseButton_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            Buttons = WindowButtonsOptions.Default with { ShowClose = false },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Primary windows must have the Close button enabled*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithZeroTitleBarHeight_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            TitleBar = TitleBarOptions.Default with { Height = 0.0 },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Title bar height must be greater than zero*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithNegativeTitleBarHeight_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            TitleBar = TitleBarOptions.Default with { Height = -10.0 },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Title bar height must be greater than zero*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithEmptyMenuProviderId_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            Menu = new MenuOptions { MenuProviderId = string.Empty },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Menu provider ID cannot be empty*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithWhitespaceMenuProviderId_ShouldThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            Menu = new MenuOptions { MenuProviderId = "   " },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Menu provider ID cannot be empty*");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_NonPrimaryWindowWithoutCloseButton_ShouldNotThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            ChromeEnabled = true,
            Buttons = WindowButtonsOptions.Default with { ShowClose = false },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().NotThrow("Tool windows can omit the Close button");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithChromeDisabledAndNoMenu_ShouldNotThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Secondary,
            ChromeEnabled = false,
            Menu = null,
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().NotThrow("System chrome without menu is valid");
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void Validate_WithValidMenu_ShouldNotThrow()
    {
        // Arrange
        var options = new WindowDecorationOptions
        {
            Category = WindowCategory.Main,
            ChromeEnabled = true,
            Menu = new MenuOptions { MenuProviderId = "App.MainMenu" },
        };

        // Act
        var act = options.Validate;

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void WithExpression_ShouldPreserveOtherProperties()
    {
        // Arrange
        var original = new WindowDecorationOptions
        {
            Category = WindowCategory.Tool,
            ChromeEnabled = true,
            TitleBar = TitleBarOptions.Default with { Height = 40.0 },
            Backdrop = BackdropKind.Mica,
        };

        // Act
        var modified = original with { Backdrop = BackdropKind.Acrylic };

        // Assert
        _ = modified.Category.Should().Be(WindowCategory.Tool);
        _ = modified.ChromeEnabled.Should().BeTrue();
        _ = modified.TitleBar.Height.Should().Be(40.0);
        _ = modified.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    [TestCategory("Unit")]
    public void DefaultValues_ShouldMatchSpecification()
    {
        // Arrange & Act
        var options = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
        };

        // Assert
        _ = options.Category.Should().Be(new WindowCategory("Test"), "Category should be set to the provided value");
        _ = options.ChromeEnabled.Should().BeTrue("ChromeEnabled defaults to true");
        _ = options.TitleBar.Should().Be(TitleBarOptions.Default, "TitleBar defaults to TitleBarOptions.Default");
        _ = options.Buttons.Should().Be(WindowButtonsOptions.Default, "Buttons defaults to WindowButtonsOptions.Default");
        _ = options.Menu.Should().BeNull("Menu defaults to null");
        _ = options.Backdrop.Should().Be(BackdropKind.None, "Backdrop defaults to None");
        _ = options.EnableSystemTitleBarOverlay.Should().BeFalse("EnableSystemTitleBarOverlay defaults to false");
    }
}
