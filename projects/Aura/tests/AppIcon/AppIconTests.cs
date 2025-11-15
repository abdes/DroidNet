// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Controls.Tests;

#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type - testing null scenarios

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class AppIconTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = default!;

    [TestMethod]
    public Task SetsTemplatePartsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 32,
            Height = 32,
        };

        // Act
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert - Verify required template parts are present
        var rootGrid = appIcon.FindDescendant<Grid>();
        _ = rootGrid.Should().NotBeNull("Template should contain a root Grid");

        var viewbox = appIcon.FindDescendant<Viewbox>();
        _ = viewbox.Should().NotBeNull("Template should contain a Viewbox for scaling");

        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull("Template should contain an IconSourceElement");
    });

    [TestMethod]
    public Task IconSource_BindsCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var symbolIcon = new SymbolIconSource { Symbol = Symbol.Home };
        var appIcon = new Controls.AppIcon
        {
            IconSource = symbolIcon,
            Width = 32,
            Height = 32,
        };

        // Act
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().Be(symbolIcon, "IconSource should be bound to the template");
    });

    [TestMethod]
    public Task IconSource_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var initialIcon = new SymbolIconSource { Symbol = Symbol.Home };
        var appIcon = new Controls.AppIcon
        {
            IconSource = initialIcon,
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement!.IconSource.Should().Be(initialIcon);

        // Act - Change the icon
        var newIcon = new SymbolIconSource { Symbol = Symbol.Accept };
        appIcon.IconSource = newIcon;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = iconElement.IconSource.Should().Be(newIcon, "IconSource should update when property changes");
    });

    [TestMethod]
    public Task IconSource_CanBeNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = null,
            Width = 32,
            Height = 32,
        };

        // Act & Assert - Should not throw
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().BeNull("Null IconSource should be handled gracefully");
    });

    [TestMethod]
    public Task IconSource_CanBeSetToNullAfterInitialization_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement!.IconSource.Should().NotBeNull();

        // Act - Clear the icon
        appIcon.IconSource = null;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = iconElement.IconSource.Should().BeNull("IconSource should be clearable");
    });

    [TestMethod]
    public Task SizeProperties_BindCorrectlyToTemplate_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const double expectedWidth = 48;
        const double expectedHeight = 48;
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = expectedWidth,
            Height = expectedHeight,
        };

        // Act
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var rootGrid = appIcon.FindDescendant<Grid>();
        _ = rootGrid.Should().NotBeNull();
        _ = rootGrid!.Width.Should().Be(expectedWidth, "Grid Width should match control Width");
        _ = rootGrid.Height.Should().Be(expectedHeight, "Grid Height should match control Height");
    });

    [TestMethod]
    public Task SizeProperties_CanBeChanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);
        var rootGrid = appIcon.FindDescendant<Grid>();

        // Act - Change size
        const double newSize = 64;
        appIcon.Width = newSize;
        appIcon.Height = newSize;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = rootGrid!.Width.Should().Be(newSize, "Grid Width should update");
        _ = rootGrid.Height.Should().Be(newSize, "Grid Height should update");
    });

    [TestMethod]
    public Task Viewbox_EnsuresProportionalScaling_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 64,
            Height = 32, // Non-square to test scaling
        };

        // Act
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var viewbox = appIcon.FindDescendant<Viewbox>();
        _ = viewbox.Should().NotBeNull("Viewbox should be present to handle proportional scaling");

        var iconElement = viewbox!.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull("IconSourceElement should be inside Viewbox");
    });

    [TestMethod]
    public Task SupportsMultipleIconTypes_SymbolIcon_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Accept },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().BeOfType<SymbolIconSource>();
    });

    [TestMethod]
    public Task SupportsMultipleIconTypes_FontIcon_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var appIcon = new Controls.AppIcon
        {
            IconSource = new FontIconSource { Glyph = "\uE10F", FontFamily = new FontFamily("Segoe MDL2 Assets") },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().BeOfType<FontIconSource>();
    });

    [TestMethod]
    public Task SupportsMultipleIconTypes_BitmapIcon_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var appIcon = new Controls.AppIcon
        {
            IconSource = new BitmapIconSource { UriSource = new Uri("ms-appx:///Assets/icon.png") },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().BeOfType<BitmapIconSource>();
    });

    [TestMethod]
    public Task DefaultStyleKey_IsSetCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
        };

        // Act
        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Assert - Control should have loaded its template successfully
        var rootGrid = appIcon.FindDescendant<Grid>();
        _ = rootGrid.Should().NotBeNull("DefaultStyleKey should enable template loading");
    });

    [TestMethod]
    public Task MultipleInstances_AreIndependent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var icon1 = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 32,
            Height = 32,
        };

        var icon2 = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Accept },
            Width = 48,
            Height = 48,
        };

        // Act
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(icon1);
        panel.Children.Add(icon2);
        await LoadTestContentAsync(panel).ConfigureAwait(true);

        // Assert - Each icon should maintain its own properties
        var icon1Element = icon1.FindDescendant<IconSourceElement>();
        var icon2Element = icon2.FindDescendant<IconSourceElement>();

        _ = icon1Element.Should().NotBeNull();
        _ = icon2Element.Should().NotBeNull();
        _ = icon1Element!.IconSource.Should().NotBe(icon2Element!.IconSource, "Each instance should be independent");
        _ = icon1.Width.Should().Be(32);
        _ = icon2.Width.Should().Be(48);
    });

    [TestMethod]
    public Task RapidPropertyChanges_DoNotCauseErrors_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var appIcon = new Controls.AppIcon
        {
            IconSource = new SymbolIconSource { Symbol = Symbol.Home },
            Width = 32,
            Height = 32,
        };

        await LoadTestContentAsync(appIcon).ConfigureAwait(true);

        // Act - Rapidly change properties
        for (var i = 0; i < 10; i++)
        {
            appIcon.IconSource = new SymbolIconSource { Symbol = (Symbol)(i % 5) };
            appIcon.Width = 16 + (i * 8);
            appIcon.Height = 16 + (i * 8);
        }

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - Should not throw and final state should be correct
        var iconElement = appIcon.FindDescendant<IconSourceElement>();
        _ = iconElement.Should().NotBeNull();
        _ = iconElement!.IconSource.Should().NotBeNull();
        _ = appIcon.Width.Should().Be(88);
    });

    protected static async Task WaitForRenderCompletion() =>
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { })
            .ConfigureAwait(true);
}

#pragma warning restore CS8625 // Cannot convert null literal to non-nullable reference type
