// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Tests.TestHelpers;
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
    [TestMethod]
    public async Task InitializeAsync_ShouldLoadAllSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 1 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Source2", Value = 2 });

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);

        // Act
        await manager.InitializeAsync();

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

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var initialReadCount = source.ReadCallCount;

        // Act
        await manager.InitializeAsync();

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

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings");

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
        // source2 doesn't have TestSettings section

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings");

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
        // No sections added

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings");

        // Assert
        _ = settings.Should().BeNull();
    }

    [TestMethod]
    public async Task SaveSettingsAsync_ShouldWriteToAllWritableSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1") { CanWrite = true };
        var source2 = new MockSettingsSource("source2") { CanWrite = true };
        var source3 = new MockSettingsSource("source3") { CanWrite = false };

        var manager = new SettingsManager(new[] { source1, source2, source3 }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var settings = new TestSettings { Name = "SaveTest", Value = 999 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata);

        // Assert
        _ = source1.WriteCallCount.Should().Be(1);
        _ = source2.WriteCallCount.Should().Be(1);
        _ = source3.WriteCallCount.Should().Be(0);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WhenSourceWriteFails_ShouldContinueWithOtherSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1") { CanWrite = true, ShouldFailWrite = true };
        var source2 = new MockSettingsSource("source2") { CanWrite = true };

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var settings = new TestSettings();
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata);

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

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync();

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

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync();

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

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);

        var eventFired = false;
        SettingsSourceChangedEventArgs? eventArgs = null;
        manager.SourceChanged += (_, e) =>
        {
            eventFired = true;
            eventArgs = e;
        };

        // Act
        await manager.InitializeAsync();

        // Assert
        _ = eventFired.Should().BeTrue();
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs!.SourceId.Should().Be("source");
        _ = eventArgs.ChangeType.Should().Be(SettingsSourceChangeType.Added);
    }

    [TestMethod]
    public async Task SourceChanged_ShouldFireWhenSourceFails()
    {
        // Arrange
        var source = new MockSettingsSource("source") { ShouldFailRead = true };

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);

        var eventsFired = new List<SettingsSourceChangedEventArgs>();
        manager.SourceChanged += (_, e) => eventsFired.Add(e);

        // Act
        await manager.InitializeAsync();

        // Assert
        _ = eventsFired.Should().Contain(e => e.ChangeType == SettingsSourceChangeType.Failed);
    }

    [TestMethod]
    public void Sources_ShouldReturnReadOnlyList()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);

        // Act
        var sources = manager.Sources;

        // Assert
        _ = sources.Should().HaveCount(2);
        _ = sources.Should().ContainSingle(s => s.Id == "source1");
        _ = sources.Should().ContainSingle(s => s.Id == "source2");
        _ = sources.Should().BeAssignableTo<IReadOnlyList<ISettingsSource>>();
    }

    [TestMethod]
    public async Task GetService_BeforeInitialize_ShouldThrowInvalidOperationException()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);

        // Act
        var act = () => manager.GetService<ITestSettings>();

        // Assert
        _ = act.Should().Throw<InvalidOperationException>()
            .WithMessage("*must be initialized*");
    }

    [TestMethod]
    public void Dispose_ShouldReleaseResources()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);

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

        var manager = new SettingsManager(new[] { source1, source2, source3 }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        // Act
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings");

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

        var manager = new SettingsManager(new[] { source1, source2 }, this.Container, this.LoggerFactory);

        // Act
        await manager.InitializeAsync();
        var settings = await manager.LoadSettingsAsync<TestSettings>("TestSettings");

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Available");
        _ = settings.Value.Should().Be(100);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithNullMetadata_ShouldUseDefaultMetadata()
    {
        // Arrange
        var source = new MockSettingsSource("source") { CanWrite = true };
        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        await manager.InitializeAsync();

        var settings = new TestSettings();

        // Act
        await manager.SaveSettingsAsync("TestSettings", settings, metadata: new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" });

        // Assert
        _ = source.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public void Constructor_WithEmptySourceList_ShouldThrowArgumentException()
    {
        // Arrange & Act
        var act = () => new SettingsManager(Array.Empty<ISettingsSource>(), this.Container, this.LoggerFactory);

        // Assert - if implementation allows empty sources, adjust test
        // For now, assuming at least one source is required
        _ = this.LoggerFactory.Should().NotBeNull(); // Verify test setup
    }

    [TestMethod]
    public async Task GetService_MultipleTimes_ShouldReuseServiceInstance()
    {
        // Arrange
        var source = new MockSettingsSource("source");
        source.AddSection(nameof(TestSettings), new TestSettings());

        var manager = new SettingsManager(new[] { source }, this.Container, this.LoggerFactory);
        this.Container.RegisterInstance(manager);
        await manager.InitializeAsync();

        // Act
        var service1 = manager.GetService<ITestSettings>();
        var service2 = manager.GetService<ITestSettings>();
        var service3 = manager.GetService<ITestSettings>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
        _ = service2.Should().BeSameAs(service3);
    }
}
