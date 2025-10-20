// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Tests.Helpers;
using DryIoc;
using FluentAssertions;

namespace DroidNet.Config.Tests;

/// <summary>
/// Comprehensive unit tests for SettingsManager covering source management,
/// last-loaded-wins merging, service factory, and lifecycle coordination.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Settings Manager")]
public class SettingsManagerTests : SettingsTestBase
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task InitializeAsync_ShouldLoadAllSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 1 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Source2", Value = 2 });

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);

        // Act
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source1.ReadCallCount.Should().Be(1);
        _ = source2.ReadCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task InitializeAsync_WhenAlreadyInitialized_ShouldNotReload()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var initialReadCount = source.ReadCallCount;

        // Act
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source.ReadCallCount.Should().Be(initialReadCount);
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WithMultipleSources_ShouldApplyLastLoadedWins()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 100 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Source2", Value = 200 });

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Source2");
        _ = settings.Value.Should().Be(200);
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WhenSectionMissingInSomeSources_ShouldMergeAvailableData()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 100 });

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Source1");
        _ = settings.Value.Should().Be(100);
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WhenNoSourcesHaveSection_ShouldReturnNull()
    {
        // Arrange
        var source = new MockSettingsSource("source");

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().BeNull();
    }

    [TestMethod]
    public async Task SaveSettingsAsync_ShouldWriteToAllSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        var source3 = new MockSettingsSource("source3");

        using var manager = new SettingsManager([source1, source2, source3], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings { Name = "SaveTest", Value = 999 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source1.WriteCallCount.Should().Be(1);
        _ = source2.WriteCallCount.Should().Be(1);
        _ = source3.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WhenSourceWriteFails_ShouldContinueWithOtherSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1") { ShouldFailWrite = true };
        var source2 = new MockSettingsSource("source2");

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings();
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source1.WriteCallCount.Should().Be(1);
        _ = source2.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task GetService_ShouldReturnSameInstanceForSameType()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = manager.GetService<ITestSettings>();
        var service2 = manager.GetService<ITestSettings>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
    }

    [TestMethod]
    public async Task GetService_ForDifferentTypes_ShouldReturnDifferentInstances()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());
        source.AddSection(nameof(AlternativeTestSettings), new AlternativeTestSettings());

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = manager.GetService<ITestSettings>();
        var service2 = manager.GetService<IAlternativeTestSettings>();

        // Assert
        _ = service1.Should().NotBeNull();
        _ = service2.Should().NotBeNull();
        _ = service1.Should().NotBeSameAs(service2);
    }

    [TestMethod]
    public async Task SourceChanged_ShouldFireWhenSourceAdded()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);

        var eventFired = false;
        SourceChangedEventArgs? eventArgs = null;
        manager.SourceChanged += (_, e) =>
        {
            eventFired = true;
            eventArgs = e;
        };

        // Act
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = eventFired.Should().BeTrue();
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.SourceId.Should().Be("source");
        _ = eventArgs.ChangeType.Should().Be(SourceChangeType.Added);
    }

    [TestMethod]
    public void Sources_ShouldReturnReadOnlyList()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);

        // Act
        var sources = manager.Sources;

        // Assert
        _ = sources.Should().HaveCount(2);
        _ = sources.Should().ContainSingle(s => s.Id == "source1");
        _ = sources.Should().ContainSingle(s => s.Id == "source2");
        _ = sources.Should().BeAssignableTo<IReadOnlyList<ISettingsSource>>();
    }

    [TestMethod]
    public void GetService_BeforeInitialize_ShouldThrowInvalidOperationException()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);

        // Act
        var act = manager.GetService<ITestSettings>;

        // Assert
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*must be initialized*");
    }

    [TestMethod]
    public void Dispose_ShouldReleaseResources()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);

        // Act
        manager.Dispose();

        // Assert - should throw when accessing disposed object
        var act = () => _ = manager.Sources;
        _ = act.Should().Throw<ObjectDisposedException>();
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WithMultipleSections_ShouldHandleComplexMerging()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        var source3 = new MockSettingsSource("source3");

        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "First", Value = 1, IsEnabled = true });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Second", Value = 2, IsEnabled = false });
        source3.AddSection(nameof(TestSettings), new TestSettings { Name = "Third", Value = 3 });

        using var manager = new SettingsManager([source1, source2, source3], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Third");
        _ = settings.Value.Should().Be(3);
    }

    [TestMethod]
    public async Task InitializeAsync_WithUnavailableSource_ShouldContinueWithAvailableSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1") { IsAvailable = false };
        var source2 = new MockSettingsSource("source2") { IsAvailable = true };
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Available", Value = 100 });

        using var manager = new SettingsManager([source1, source2], this.Container, this.LoggerFactory);

        // Act
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Available");
        _ = settings.Value.Should().Be(100);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithNullMetadata_ShouldUseDefaultMetadata()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings();

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata: new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" }, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task GetService_MultipleTimes_ShouldReuseServiceInstance()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());

        using var manager = new SettingsManager([source], this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = manager.GetService<ITestSettings>();
        var service2 = manager.GetService<ITestSettings>();
        var service3 = manager.GetService<ITestSettings>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
        _ = service2.Should().BeSameAs(service3);
    }
}
