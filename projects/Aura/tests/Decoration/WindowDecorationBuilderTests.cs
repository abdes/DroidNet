// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using FluentAssertions;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Unit tests for <see cref="WindowDecorationBuilder"/>.
/// </summary>
[TestClass]
[TestCategory("Window Decoration Builder")]
public class WindowDecorationBuilderTests
{
    [TestMethod]
    [TestCategory("Presets")]
    public void ForPrimaryWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForMainWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Main);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.TitleBar.Height.Should().Be(40.0);
        _ = options.Buttons.ShowMinimize.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeTrue();
        _ = options.Buttons.ShowClose.Should().BeTrue();
        _ = options.Backdrop.Should().Be(BackdropKind.Mica);
        _ = options.Menu.Should().BeNull();
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void ForDocumentWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForDocumentWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Document);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.TitleBar.Height.Should().Be(32.0);
        _ = options.Buttons.ShowMinimize.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeTrue();
        _ = options.Buttons.ShowClose.Should().BeTrue();
        _ = options.Backdrop.Should().Be(BackdropKind.Mica);
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void ForToolWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForToolWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Tool);
        _ = options.ChromeEnabled.Should().BeFalse("tool windows use custom compact chrome");
        _ = options.TitleBar.Height.Should().Be(24.0, "tool windows use compact title bar");
        _ = options.Buttons.ShowMinimize.Should().BeFalse("tool windows typically don't minimize");
        _ = options.Buttons.ShowMaximize.Should().BeFalse("tool windows typically don't maximize");
        _ = options.Buttons.ShowClose.Should().BeTrue();
        _ = options.Backdrop.Should().Be(BackdropKind.MicaAlt);
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void ForSecondaryWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForSecondaryWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Secondary);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.Buttons.ShowMinimize.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeTrue();
        _ = options.Buttons.ShowClose.Should().BeTrue();
        _ = options.Backdrop.Should().Be(BackdropKind.MicaAlt);
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void ForTransientWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForTransientWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Transient);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeFalse();
        _ = options.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void ForModalWindow_BuildsValidOptions()
    {
        // Act
        var options = WindowDecorationBuilder.ForModalWindow().Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Modal);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeFalse();
        _ = options.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    [TestCategory("Presets")]
    public void Build_WithNoCustomization_UsesDefaults()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.Build();

        // Assert - defaults from a freshly constructed builder
        _ = options.Category.Should().Be(WindowCategory.System);
        _ = options.ChromeEnabled.Should().BeTrue();
        _ = options.TitleBar.Height.Should().Be(TitleBarOptions.Default.Height);
        _ = options.Buttons.Should().Be(WindowButtonsOptions.Default);
        _ = options.Menu.Should().BeNull();
        _ = options.Backdrop.Should().Be(BackdropKind.None);
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithCategory_SetsCategory()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithCategory(new("Custom")).Build();

        // Assert
        _ = options.Category.Should().Be(new WindowCategory("Custom"));
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithChrome_SetsChromeEnabled()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithChrome(false).Build();

        // Assert
        _ = options.ChromeEnabled.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithTitleBar_SetsTitleBarOptions()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();
        var titleBar = new TitleBarOptions { Height = 50.0, ShowTitle = false };

        // Act
        var options = builder.WithTitleBar(titleBar).Build();

        // Assert
        _ = options.TitleBar.Height.Should().Be(50.0);
        _ = options.TitleBar.ShowTitle.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    [TestCategory("Validation")]
    public void WithTitleBar_ThrowsOnNull()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var act = () => builder.WithTitleBar(null!);

        // Assert
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("titleBar");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithButtons_SetsButtonOptions()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();
        var buttons = new WindowButtonsOptions { ShowMaximize = false };

        // Act
        var options = builder.WithButtons(buttons).Build();

        // Assert
        _ = options.Buttons.ShowMaximize.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    [TestCategory("Validation")]
    public void WithButtons_ThrowsOnNull()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var act = () => builder.WithButtons(null!);

        // Assert
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("buttons");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithMenu_SetsMenuOptions()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();
        var menu = new MenuOptions { MenuProviderId = "Test.Menu" };

        // Act
        var options = builder.WithMenu(menu).Build();

        // Assert
        _ = options.Menu.Should().NotBeNull();
        _ = options.Menu!.MenuProviderId.Should().Be("Test.Menu");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithMenu_AcceptsNull()
    {
        // Arrange
        var builder = WindowDecorationBuilder.ForMainWindow();

        // Act
        var options = builder.WithMenu((MenuOptions?)null).Build();

        // Assert
        _ = options.Menu.Should().BeNull();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithMenu_WithProviderId_CreatesMenuOptions()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithMenu("Test.Menu", isCompact: true).Build();

        // Assert
        _ = options.Menu.Should().NotBeNull();
        _ = options.Menu!.MenuProviderId.Should().Be("Test.Menu");
        _ = options.Menu.IsCompact.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    [TestCategory("Validation")]
    public void WithMenu_WithProviderId_ThrowsOnNullProviderId()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var act = () => builder.WithMenu(null!, isCompact: false);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithBackdrop_SetsBackdropKind()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithBackdrop(BackdropKind.Acrylic).Build();

        // Assert
        _ = options.Backdrop.Should().Be(BackdropKind.Acrylic);
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithSystemTitleBarOverlay_SetsFlag()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithSystemTitleBarOverlay(true).Build();

        // Assert
        _ = options.EnableSystemTitleBarOverlay.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void WithTitleBarHeight_SetsTitleBarHeight()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.WithTitleBarHeight(45.0).Build();

        // Assert
        _ = options.TitleBar.Height.Should().Be(45.0);
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    [TestCategory("Validation")]
    public void WithTitleBarHeight_ThrowsOnZero()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var act = () => builder.WithTitleBarHeight(0.0);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>()
            .WithParameterName("height");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    [TestCategory("Validation")]
    public void WithTitleBarHeight_ThrowsOnNegative()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var act = () => builder.WithTitleBarHeight(-10.0);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>()
            .WithParameterName("height");
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void NoMaximize_HidesMaximizeButton()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.NoMaximize().Build();

        // Assert
        _ = options.Buttons.ShowMaximize.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void NoMinimize_HidesMinimizeButton()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act
        var options = builder.NoMinimize().Build();

        // Assert
        _ = options.Buttons.ShowMinimize.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Fluent API")]
    public void NoBackdrop_DisablesBackdrop()
    {
        // Arrange
        var builder = WindowDecorationBuilder.ForMainWindow();

        // Act
        var options = builder.NoBackdrop().Build();

        // Assert
        _ = options.Backdrop.Should().Be(BackdropKind.None);
    }

    [TestMethod]
    [TestCategory("Method Chaining")]
    public void FluentMethods_ReturnSameBuilder()
    {
        // Arrange
        var builder = new WindowDecorationBuilder();

        // Act & Assert
        _ = builder.WithCategory(new("Test")).Should().BeSameAs(builder);
        _ = builder.WithChrome(true).Should().BeSameAs(builder);
        _ = builder.WithBackdrop(BackdropKind.Mica).Should().BeSameAs(builder);
        _ = builder.NoMaximize().Should().BeSameAs(builder);
        _ = builder.NoMinimize().Should().BeSameAs(builder);
        _ = builder.NoBackdrop().Should().BeSameAs(builder);
    }

    [TestMethod]
    [TestCategory("Customization")]
    public void PresetCustomization_PreservesNonCustomizedProperties()
    {
        // Arrange & Act
        var options = WindowDecorationBuilder.ForMainWindow()
            .WithTitleBarHeight(50.0)
            .Build();

        // Assert - customized property
        _ = options.TitleBar.Height.Should().Be(50.0);

        // Assert - preserved preset properties
        _ = options.Category.Should().Be(WindowCategory.Main);
        _ = options.Backdrop.Should().Be(BackdropKind.Mica);
        _ = options.Buttons.ShowMinimize.Should().BeTrue();
        _ = options.Buttons.ShowMaximize.Should().BeTrue();
        _ = options.Buttons.ShowClose.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory("Customization")]
    public void ComplexCustomization_WorksCorrectly()
    {
        // Arrange & Act - Start with ForSecondaryWindow which has chrome enabled
        var options = WindowDecorationBuilder.ForSecondaryWindow()
            .WithMenu("App.ToolMenu", isCompact: true)
            .WithTitleBarHeight(36.0)
            .WithBackdrop(BackdropKind.Acrylic)
            .NoMinimize()
            .NoMaximize()
            .Build();

        // Assert
        _ = options.Category.Should().Be(WindowCategory.Secondary);
        _ = options.Menu.Should().NotBeNull();
        _ = options.Menu!.MenuProviderId.Should().Be("App.ToolMenu");
        _ = options.Menu.IsCompact.Should().BeTrue();
        _ = options.TitleBar.Height.Should().Be(36.0);
        _ = options.Backdrop.Should().Be(BackdropKind.Acrylic);
        _ = options.Buttons.ShowMinimize.Should().BeFalse();
        _ = options.Buttons.ShowMaximize.Should().BeFalse();
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void Build_CallsValidateAndThrowsOnInvalidOptions()
    {
        // Arrange - chrome disabled with menu is invalid
        var builder = new WindowDecorationBuilder()
            .WithChrome(false)
            .WithMenu("App.Menu");

        // Act
        var act = builder.Build;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*chrome*menu*");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void Build_ThrowsOnPrimaryWindowWithoutCloseButton()
    {
        // Arrange
        var builder = WindowDecorationBuilder.ForMainWindow()
            .WithButtons(new WindowButtonsOptions { ShowClose = false });

        // Act
        var act = builder.Build;

        // Assert
        _ = act.Should().Throw<ValidationException>()
            .WithMessage("*Primary*close*");
    }

    [TestMethod]
    public void MenuProviderIds_HasExpectedConstants()
    {
        // Assert
        _ = MenuProviderIds.MainMenu.Should().Be("App.MainMenu");
        _ = MenuProviderIds.ContextMenu.Should().Be("App.ContextMenu");
        _ = MenuProviderIds.ToolMenu.Should().Be("App.ToolMenu");
    }
}
