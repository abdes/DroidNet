// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using AwesomeAssertions;
using Microsoft.Extensions.DependencyInjection;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Unit tests for <see cref="IMenuProvider"/>, <see cref="MenuProvider"/>,
///     and <see cref="ScopedMenuProvider"/>.
/// </summary>
[TestClass]
[TestCategory("Menu Provider")]
public partial class MenuProviderTests
{
    [TestMethod]
    [TestCategory("Validation")]
    public void MenuProvider_Constructor_ThrowsOnNullProviderId()
    {
        // Arrange & Act
        var act = () => new MenuProvider(null!, () => new MenuBuilder());

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void MenuProvider_Constructor_ThrowsOnEmptyProviderId()
    {
        // Arrange & Act
        var act = () => new MenuProvider(string.Empty, () => new MenuBuilder());

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void MenuProvider_Constructor_ThrowsOnWhitespaceProviderId()
    {
        // Arrange & Act
        var act = () => new MenuProvider("   ", () => new MenuBuilder());

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void MenuProvider_Constructor_ThrowsOnNullBuilderFactory()
    {
        // Arrange & Act
        var act = () => new MenuProvider("test.provider", null!);

        // Assert
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("builderFactory");
    }

    [TestMethod]
    public void MenuProvider_ProviderId_ReturnsCorrectValue()
    {
        // Arrange
        const string expectedId = "App.MainMenu";
        var provider = new MenuProvider(expectedId, () => new MenuBuilder());

        // Act
        var actualId = provider.ProviderId;

        // Assert
        _ = actualId.Should().Be(expectedId);
    }

    [TestMethod]
    public void MenuProvider_CreateMenuSource_ReturnsValidMenuSource()
    {
        // Arrange
        var provider = new MenuProvider(
            "test.provider",
            () => new MenuBuilder()
                .AddMenuItem("File", command: null, icon: null, acceleratorText: null)
                .AddMenuItem("Edit", command: null, icon: null, acceleratorText: null));

        // Act
        var menuSource = provider.CreateMenuSource();

        // Assert
        _ = menuSource.Should().NotBeNull();
        _ = menuSource.Items.Should().HaveCount(2);
        _ = menuSource.Items[0].Text.Should().Be("File");
        _ = menuSource.Items[1].Text.Should().Be("Edit");
    }

    [TestMethod]
    public void MenuProvider_CreateMenuSource_ReturnsDistinctInstances()
    {
        // Arrange
        var provider = new MenuProvider(
            "test.provider",
            () => new MenuBuilder().AddMenuItem("Test", command: null, icon: null, acceleratorText: null));

        // Act
        var menuSource1 = provider.CreateMenuSource();
        var menuSource2 = provider.CreateMenuSource();

        // Assert
        _ = menuSource1.Should().NotBeSameAs(menuSource2);
        _ = menuSource1.Items.Should().NotBeSameAs(menuSource2.Items);
    }

    [TestMethod]
    public async Task MenuProvider_CreateMenuSource_IsThreadSafe()
    {
        // Arrange
        var provider = new MenuProvider(
            "test.provider",
            () => new MenuBuilder().AddMenuItem("Test", command: null, icon: null, acceleratorText: null));

        var sources = new IMenuSource[100];
        var tasks = new Task[100];

        // Act - Create menu sources concurrently
        for (var i = 0; i < 100; i++)
        {
            var index = i;
            tasks[i] = Task.Run(() => sources[index] = provider.CreateMenuSource());
        }

        await Task.WhenAll(tasks).ConfigureAwait(true);

        // Assert - All sources should be created successfully
        _ = sources.Should().AllSatisfy(s => s.Should().NotBeNull());

        // All sources should be distinct instances
        var distinctSources = sources.Distinct().ToList();
        _ = distinctSources.Should().HaveCount(100);
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void ScopedMenuProvider_Constructor_ThrowsOnNullProviderId()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();

        // Act
        var act = () => new ScopedMenuProvider(null!, services, (_, _) => { });

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void ScopedMenuProvider_Constructor_ThrowsOnEmptyProviderId()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();

        // Act
        var act = () => new ScopedMenuProvider(string.Empty, services, (_, _) => { });

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithParameterName("providerId");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void ScopedMenuProvider_Constructor_ThrowsOnNullServiceProvider()
    {
        // Arrange & Act
        var act = () => new ScopedMenuProvider("test.provider", null!, (_, _) => { });

        // Assert
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("serviceProvider");
    }

    [TestMethod]
    [TestCategory("Validation")]
    public void ScopedMenuProvider_Constructor_ThrowsOnNullConfigureAction()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();

        // Act
        var act = () => new ScopedMenuProvider("test.provider", services, null!);

        // Assert
        _ = act.Should().Throw<ArgumentNullException>()
            .WithParameterName("configureMenu");
    }

    [TestMethod]
    public void ScopedMenuProvider_ProviderId_ReturnsCorrectValue()
    {
        // Arrange
        const string expectedId = "App.ScopedMenu";
        var services = new ServiceCollection().BuildServiceProvider();
        var provider = new ScopedMenuProvider(expectedId, services, (_, _) => { });

        // Act
        var actualId = provider.ProviderId;

        // Assert
        _ = actualId.Should().Be(expectedId);
    }

    [TestMethod]
    public void ScopedMenuProvider_CreateMenuSource_ResolvesServicesDuringConfiguration()
    {
        // Arrange
        var testService = new TestCommandService();
        var services = new ServiceCollection();
        _ = services.AddSingleton<ITestCommandService>(testService);
        var serviceProvider = services.BuildServiceProvider();

        var provider = new ScopedMenuProvider(
            "test.provider",
            serviceProvider,
            (builder, sp) =>
            {
                var commandService = sp.GetRequiredService<ITestCommandService>();
#pragma warning disable IDE0058 // Expression value is never used (lambda expression
                builder.AddMenuItem("Command", commandService.TestCommand);
#pragma warning restore IDE0058 // Expression value is never used
            });

        // Act
        var menuSource = provider.CreateMenuSource();

        // Assert
        _ = menuSource.Should().NotBeNull();
        _ = menuSource.Items.Should().HaveCount(1);
        _ = menuSource.Items[0].Text.Should().Be("Command");
        _ = menuSource.Items[0].Command.Should().BeSameAs(testService.TestCommand);
    }

    [TestMethod]
    public void ScopedMenuProvider_CreateMenuSource_ReturnsDistinctInstances()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();
        var provider = new ScopedMenuProvider(
            "test.provider",
            services,
            (builder, _) => builder.AddMenuItem("Test", command: null, icon: null, acceleratorText: null));

        // Act
        var menuSource1 = provider.CreateMenuSource();
        var menuSource2 = provider.CreateMenuSource();

        // Assert
        _ = menuSource1.Should().NotBeSameAs(menuSource2);
        _ = menuSource1.Items.Should().NotBeSameAs(menuSource2.Items);
    }

    [TestMethod]
    public async Task ScopedMenuProvider_CreateMenuSource_IsThreadSafe()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();
        var provider = new ScopedMenuProvider(
            "test.provider",
            services,
            (builder, _) => builder.AddMenuItem("Test", command: null, icon: null, acceleratorText: null));

        var sources = new IMenuSource[100];
        var tasks = new Task[100];

        // Act - Create menu sources concurrently
        for (var i = 0; i < 100; i++)
        {
            var index = i;
            tasks[i] = Task.Run(() => sources[index] = provider.CreateMenuSource());
        }

        await Task.WhenAll(tasks).ConfigureAwait(true);

        // Assert - All sources should be created successfully
        _ = sources.Should().AllSatisfy(s => s.Should().NotBeNull());

        // All sources should be distinct instances
        var distinctSources = sources.Distinct().ToList();
        _ = distinctSources.Should().HaveCount(100);
    }

    [TestMethod]
    public void ScopedMenuProvider_CreateMenuSource_UsesRegisteredMenuBuilder()
    {
        // Arrange
        var customBuilder = new MenuBuilder();
        var services = new ServiceCollection();
        _ = services.AddSingleton(customBuilder);
        var serviceProvider = services.BuildServiceProvider();

        var wasConfigured = false;
        var provider = new ScopedMenuProvider(
            "test.provider",
            serviceProvider,
            (builder, _) =>
            {
                wasConfigured = true;
#pragma warning disable IDE0058 // Expression value is never used (lambda expression
                builder.AddMenuItem("Test", command: null, icon: null, acceleratorText: null);
#pragma warning restore IDE0058 // Expression value is never used
            });

        // Act
        var menuSource = provider.CreateMenuSource();

        // Assert
        _ = wasConfigured.Should().BeTrue();
        _ = menuSource.Should().NotBeNull();
    }

    [TestMethod]
    public void ScopedMenuProvider_CreateMenuSource_CreatesDefaultMenuBuilderWhenNotRegistered()
    {
        // Arrange
        var services = new ServiceCollection().BuildServiceProvider();
        var provider = new ScopedMenuProvider(
            "test.provider",
            services,
            (builder, _) => builder.AddMenuItem("Test", command: null, icon: null, acceleratorText: null));

        // Act
        var menuSource = provider.CreateMenuSource();

        // Assert
        _ = menuSource.Should().NotBeNull();
        _ = menuSource.Items.Should().HaveCount(1);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.OrderingRules", "SA1201:Elements should appear in the correct order", Justification = "better for test file organization")]
    private interface ITestCommandService
    {
        public System.Windows.Input.ICommand TestCommand { get; }
    }

    private sealed class TestCommandService : ITestCommandService
    {
        public System.Windows.Input.ICommand TestCommand { get; } = new TestCommand();
    }

    private sealed partial class TestCommand : System.Windows.Input.ICommand
    {
#pragma warning disable CS0067 // Used by Menus
        public event EventHandler? CanExecuteChanged;
#pragma warning restore CS0067

        public bool CanExecute(object? parameter) => true;

        public void Execute(object? parameter)
        {
            // No-op for testing
        }
    }
}
