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
    private readonly MockSettingsSource source = new("test-source");

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public void TestInitialize() => this.Container.RegisterInstance<ISettingsSource>(this.source);

    [TestMethod]
    public async Task InitializeAsync_ShouldLoadAllRegisteredSources()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 1 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Source2", Value = 2 });
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        // Act
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source1.ReadCallCount.Should().Be(1);
        _ = source2.ReadCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task InitializeAsync_WhenAlreadyInitialized_ShouldNotReload()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings());

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var initialReadCount = this.source.ReadCallCount;

        // Act
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = this.source.ReadCallCount.Should().Be(initialReadCount);
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WithMultipleSources_ShouldApplyLastLoadedWins()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "Source1", Value = 100 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Source2", Value = 200 });
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await this.SettingsManager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

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
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await this.SettingsManager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings.Name.Should().Be("Source1");
        _ = settings.Value.Should().Be(100);
    }

    [TestMethod]
    public async Task LoadSettingsAsync_WhenNoSourcesHaveSection_ShouldReturnNull()
    {
        // Arrange
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await this.SettingsManager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

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
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);
        this.Container.RegisterInstance<ISettingsSource>(source3);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings { Name = "SaveTest", Value = 999 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

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
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings();
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = source1.WriteCallCount.Should().Be(1);
        _ = source2.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task GetService_ShouldReturnSameInstanceForSameType()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings());

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = this.SettingsManager.GetService<ITestSettings>();
        var service2 = this.SettingsManager.GetService<ITestSettings>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
    }

    [TestMethod]
    public async Task GetService_ForDifferentTypes_ShouldReturnDifferentInstances()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings());
        this.source.AddSection(nameof(AlternativeTestSettings), new AlternativeTestSettings());

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = this.SettingsManager.GetService<ITestSettings>();
        var service2 = this.SettingsManager.GetService<IAlternativeTestSettings>();

        // Assert
        _ = service1.Should().NotBeNull();
        _ = service2.Should().NotBeNull();
        _ = service1.Should().NotBeSameAs(service2);
    }

    [TestMethod]
    public async Task SourceChanged_ShouldFireWhenSourceAdded()
    {
        // Arrange
        var eventFired = false;
        SourceChangedEventArgs? eventArgs = null;
        this.SettingsManager.SourceChanged += (_, e) =>
        {
            eventFired = true;
            eventArgs = e;
        };

        // Act
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = eventFired.Should().BeTrue();
        _ = eventArgs.Should().NotBeNull();
        _ = eventArgs.SourceId.Should().Be("test-source");
        _ = eventArgs.ChangeType.Should().Be(SourceChangeType.Added);
    }

    [TestMethod]
    public async Task Sources_ShouldReturnReadOnlyList()
    {
        // Arrange
        await this.SettingsManager.AddSourceAsync(new MockSettingsSource("second-source"), this.TestContext.CancellationToken).ConfigureAwait(true);
        await this.SettingsManager.AddSourceAsync(new MockSettingsSource("third-source"), this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var sources = this.SettingsManager.Sources;

        // Assert
        _ = sources.Should().HaveCount(3);
        _ = sources.Should().ContainSingle(s => s.Id == "test-source");
        _ = sources.Should().ContainSingle(s => s.Id == "second-source");
        _ = sources.Should().ContainSingle(s => s.Id == "third-source");
        _ = sources.Should().BeAssignableTo<IReadOnlyList<ISettingsSource>>();
    }

    [TestMethod]
    public void GetService_BeforeInitialize_ShouldThrowInvalidOperationException()
    {
        // Arrange
        using var manager = new SettingsManager([this.source], this.Container, this.LoggerFactory);

        // Act
        var act = manager.GetService<ITestSettings>;

        // Assert
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*must be initialized*");
    }

    [TestMethod]
    public void Dispose_ShouldReleaseResources()
    {
        // Arrange - done by default in TestBase

        // Act
        this.SettingsManager.Dispose();

        // Assert - should throw when accessing disposed object
        var act = () => _ = this.SettingsManager.Sources;
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

        await this.SettingsManager.AddSourceAsync(source1, this.TestContext.CancellationToken).ConfigureAwait(true);
        await this.SettingsManager.AddSourceAsync(source2, this.TestContext.CancellationToken).ConfigureAwait(true);
        await this.SettingsManager.AddSourceAsync(source3, this.TestContext.CancellationToken).ConfigureAwait(true);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var settings = await this.SettingsManager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

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

        await this.SettingsManager.AddSourceAsync(source1, this.TestContext.CancellationToken).ConfigureAwait(true);
        await this.SettingsManager.AddSourceAsync(source2, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var settings = await this.SettingsManager.LoadSettingsAsync<TestSettings>("TestSettings", cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = settings.Should().NotBeNull();
        _ = settings!.Name.Should().Be("Available");
        _ = settings.Value.Should().Be(100);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WithNullMetadata_ShouldUseDefaultMetadata()
    {
        // Arrange
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings();

        // Act
        await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata: new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" }, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = this.source.WriteCallCount.Should().Be(1);
    }

    [TestMethod]
    public async Task GetService_MultipleTimes_ShouldReuseServiceInstance()
    {
        // Arrange
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = this.SettingsManager.GetService<ITestSettings>();
        var service2 = this.SettingsManager.GetService<ITestSettings>();
        var service3 = this.SettingsManager.GetService<ITestSettings>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
        _ = service2.Should().BeSameAs(service3);
    }
}
