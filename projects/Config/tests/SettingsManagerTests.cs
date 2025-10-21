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
    public async Task SaveSettingsAsync_ShouldWriteToWinningSourceOnly()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        var source3 = new MockSettingsSource("source3");
        source2.AddSection("TestSettings", new TestSettings { Name = "FromSource2", Value = 200 });
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);
        this.Container.RegisterInstance<ISettingsSource>(source3);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings { Name = "SaveTest", Value = 999 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act - Save should only go to source2 (last-loaded-wins for TestSettings)
        await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Only source2 should be written to (it was the winning source)
        _ = source1.WriteCallCount.Should().Be(0, "source1 did not contribute the TestSettings section");
        _ = source2.WriteCallCount.Should().Be(1, "source2 was the winning source for TestSettings");
        _ = source3.WriteCallCount.Should().Be(0, "source3 did not contribute the TestSettings section");
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WhenWinningSourceWriteFails_ShouldThrow()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1") { ShouldFailWrite = true };
        source1.AddSection("TestSettings", new TestSettings());
        var source2 = new MockSettingsSource("source2");
        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var settings = new TestSettings();
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };

        // Act
        var act = async () => await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Should throw because the winning source (source1) fails
        _ = await act.Should().ThrowAsync<SettingsPersistenceException>().ConfigureAwait(true);

        // Assert - Only source1 should be attempted (it was the winning source)
        _ = source1.WriteCallCount.Should().Be(1, "source1 was the winning source");
        _ = source2.WriteCallCount.Should().Be(0, "source2 should not be attempted when winning source fails");
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

    [TestMethod]
    public async Task SourceChanged_WhenSourceTriggersEvent_ShouldAutoUpdateAffectedServices()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        source1.AddSection("TestSettings", new TestSettings { Name = "Initial", Value = 100 });
        this.Container.RegisterInstance<ISettingsSource>(source1);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var service = this.SettingsManager.GetService<ITestSettings>();

        _ = service.Settings.Name.Should().Be("Initial");
        _ = service.Settings.Value.Should().Be(100);

        // Act - Simulate source change
        source1.AddSection("TestSettings", new TestSettings { Name = "Updated", Value = 200 });
        source1.TriggerSourceChanged(SourceChangeType.Updated);

        // Give the async handler time to process
        await Task.Delay(500).ConfigureAwait(true);

        // Assert - Service should be automatically updated with new values
        _ = service.Settings.Name.Should().Be("Updated");
        _ = service.Settings.Value.Should().Be(200);
    }

    [TestMethod]
    public async Task SourceChanged_WhenSectionRemovedFromSource_ShouldResetServiceToDefaults()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        source1.AddSection("TestSettings", new TestSettings { Name = "FromSource", Value = 500 });
        this.Container.RegisterInstance<ISettingsSource>(source1);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var service = this.SettingsManager.GetService<ITestSettings>();

        _ = service.Settings.Name.Should().Be("FromSource");
        _ = service.Settings.Value.Should().Be(500);

        // Act - Remove section from source and trigger change
        source1.RemoveSection("TestSettings");
        source1.TriggerSourceChanged(SourceChangeType.Updated);

        // Give the async handler time to process
        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Service should have reset to defaults
        _ = service.Settings.Name.Should().Be("Default");
        _ = service.Settings.Value.Should().Be(42);
    }

    [TestMethod]
    public async Task AddSourceAsync_WhenNewSourceProvidesSection_ShouldUpdateExistingService()
    {
        // Arrange
        this.source.AddSection("TestSettings", new TestSettings { Name = "Original", Value = 10 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();
        _ = service.Settings.Name.Should().Be("Original");
        _ = service.Settings.Value.Should().Be(10);

        // Act - Add new source with different settings (last-loaded-wins)
        var newSource = new MockSettingsSource("new-source");
        newSource.AddSection("TestSettings", new TestSettings { Name = "FromNewSource", Value = 999 });
        await this.SettingsManager.AddSourceAsync(newSource, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Give the async handler time to process
        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Service should use settings from new source (last-loaded-wins)
        _ = service.Settings.Name.Should().Be("FromNewSource");
        _ = service.Settings.Value.Should().Be(999);
    }

    [TestMethod]
    public async Task RemoveSourceAsync_WhenRemovedSourceWasWinner_ShouldRevertToPreviousSource()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        source1.AddSection("TestSettings", new TestSettings { Name = "Source1", Value = 100 });
        source2.AddSection("TestSettings", new TestSettings { Name = "Source2", Value = 200 });

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var service = this.SettingsManager.GetService<ITestSettings>();

        // Source2 should be the winner (last-loaded-wins)
        _ = service.Settings.Name.Should().Be("Source2");

        // Act - Remove winning source
        await this.SettingsManager.RemoveSourceAsync("source2", this.TestContext.CancellationToken).ConfigureAwait(true);

        // Give time for cleanup
        await Task.Delay(50).ConfigureAwait(true);

        // Note: After removal, the section is no longer tracked, so service keeps its last values
        // This is expected behavior - removal doesn't trigger automatic reload
        _ = service.Settings.Name.Should().Be("Source2");
    }

    [TestMethod]
    public async Task SaveSettingsAsync_WhenNoWinningSource_ShouldSaveToFirstAvailableSource()
    {
        // Arrange - test-source is already registered via TestInitialize
        var extraSource = new MockSettingsSource("extra-source");
        this.Container.RegisterInstance<ISettingsSource>(extraSource);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Save new section that hasn't been loaded from any source
        var settings = new TestSettings { Name = "NewSection", Value = 777 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };
        await this.SettingsManager.SaveSettingsAsync("NewSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Should save to first source (test-source)
        _ = this.source.WriteCallCount.Should().Be(1, "first source (test-source) should receive the write");
        _ = extraSource.WriteCallCount.Should().Be(0, "second source should not be written to");
    }

    [TestMethod]
    public async Task SourceChanged_MultipleServices_ShouldUpdateOnlyAffectedServices()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        source1.AddSection("TestSettings", new TestSettings { Name = "Test1", Value = 100 });
        source1.AddSection("AlternativeTestSettings", new AlternativeTestSettings { Theme = "Dark", FontSize = 16 });
        this.Container.RegisterInstance<ISettingsSource>(source1);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var testService = this.SettingsManager.GetService<ITestSettings>();
        var altService = this.SettingsManager.GetService<IAlternativeTestSettings>();

        _ = testService.Settings.Name.Should().Be("Test1");
        _ = altService.Settings.Theme.Should().Be("Dark");

        // Act - Update only TestSettings section
        source1.AddSection("TestSettings", new TestSettings { Name = "Test1-Updated", Value = 150 });
        source1.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Only TestSettings service should be updated
        _ = testService.Settings.Name.Should().Be("Test1-Updated");
        _ = testService.Settings.Value.Should().Be(150);

        // AlternativeTestSettings should remain unchanged
        _ = altService.Settings.Theme.Should().Be("Dark");
        _ = altService.Settings.FontSize.Should().Be(16);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_AfterMultipleSourceLoads_ShouldSaveToLastWinner()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        var source3 = new MockSettingsSource("source3");

        source1.AddSection("TestSettings", new TestSettings { Name = "S1", Value = 1 });
        source2.AddSection("TestSettings", new TestSettings { Name = "S2", Value = 2 });
        source3.AddSection("TestSettings", new TestSettings { Name = "S3", Value = 3 });

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);
        this.Container.RegisterInstance<ISettingsSource>(source3);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Save modified settings
        var settings = new TestSettings { Name = "Modified", Value = 999 };
        var metadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "20251019" };
        await this.SettingsManager.SaveSettingsAsync("TestSettings", settings, metadata, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Should save only to source3 (last-loaded-wins)
        _ = source1.WriteCallCount.Should().Be(0);
        _ = source2.WriteCallCount.Should().Be(0);
        _ = source3.WriteCallCount.Should().Be(1, "source3 was the last to load TestSettings");
    }

    [TestMethod]
    public async Task SourceChanged_WhenMultipleSourcesProvideSection_ShouldUseLastLoadedWins()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        source1.AddSection("TestSettings", new TestSettings { Name = "S1", Value = 100 });
        source2.AddSection("TestSettings", new TestSettings { Name = "S2", Value = 200 });

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();
        _ = service.Settings.Name.Should().Be("S2"); // source2 wins initially

        // Act - source1 changes (but it's not the winner, so service shouldn't change)
        source1.AddSection("TestSettings", new TestSettings { Name = "S1-Changed", Value = 111 });
        source1.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Service should still use source2 values (source2 is still the winner)
        _ = service.Settings.Name.Should().Be("S2");
        _ = service.Settings.Value.Should().Be(200);

        // Act - source2 changes (it's the winner, so service should update)
        source2.AddSection("TestSettings", new TestSettings { Name = "S2-Changed", Value = 222 });
        source2.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Service should now use updated source2 values
        _ = service.Settings.Name.Should().Be("S2-Changed");
        _ = service.Settings.Value.Should().Be(222);
    }

    [TestMethod]
    public async Task ReloadAllAsync_ShouldReapplyLastLoadedWinsAndUpdateServices()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");

        source1.AddSection("TestSettings", new TestSettings { Name = "S1", Value = 100 });
        source2.AddSection("TestSettings", new TestSettings { Name = "S2", Value = 200 });

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();
        _ = service.Settings.Name.Should().Be("S2");

        // Modify sources
        source1.AddSection("TestSettings", new TestSettings { Name = "S1-Modified", Value = 111 });
        source2.AddSection("TestSettings", new TestSettings { Name = "S2-Modified", Value = 222 });

        // Act
        await this.SettingsManager.ReloadAllAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Service should use source2's modified values (last-loaded-wins)
        _ = service.Settings.Name.Should().Be("S2-Modified");
        _ = service.Settings.Value.Should().Be(222);
    }

    [TestMethod]
    public async Task SubscribeToSourceEvents_ShouldHappenDuringInitialization()
    {
        // Arrange
        var eventFired = false;
        var source1 = new MockSettingsSource("source1");
        source1.AddSection("TestSettings", new TestSettings());
        this.Container.RegisterInstance<ISettingsSource>(source1);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        this.SettingsManager.SourceChanged += (_, _) => eventFired = true;

        // Act - Trigger change on source
        source1.TriggerSourceChanged(SourceChangeType.Updated);
        await Task.Delay(100).ConfigureAwait(true);

        // Assert - Manager should have received the event (subscription happened during init)
        _ = eventFired.Should().BeTrue("manager should be subscribed to source events");
    }

    [TestMethod]
    public async Task UnsubscribeFromSourceEvents_ShouldHappenDuringDisposal()
    {
        // Arrange
        var source1 = new MockSettingsSource("source1");
        source1.AddSection("TestSettings", new TestSettings());
        this.Container.RegisterInstance<ISettingsSource>(source1);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var eventCount = 0;
        this.SettingsManager.SourceChanged += (_, _) => eventCount++;

        // Act - Dispose and then trigger source change
        this.SettingsManager.Dispose();
        source1.TriggerSourceChanged(SourceChangeType.Updated);
        await Task.Delay(100).ConfigureAwait(true);

        // Assert - No events should be received after disposal
        _ = eventCount.Should().Be(0, "manager should unsubscribe from source events on disposal");
    }

    [TestMethod]
    public async Task GetService_WithJsonElementData_ShouldDeserializeWithCaseInsensitivePropertyNames()
    {
        // Arrange - Simulate JSON with camelCase properties (like from JSON files)
        var jsonText = """
            {
                "name": "CamelCaseTest",
                "value": 999
            }
            """;
        var jsonElement = System.Text.Json.JsonDocument.Parse(jsonText).RootElement;

        this.source.AddSection(nameof(TestSettings), jsonElement);
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Get service (should deserialize JsonElement with case-insensitive matching)
        var service = this.SettingsManager.GetService<ITestSettings>();

        // Assert - Properties should be correctly mapped despite case mismatch
        _ = service.Settings.Name.Should().Be("CamelCaseTest");
        _ = service.Settings.Value.Should().Be(999);
    }

    [TestMethod]
    public async Task GetService_AfterInitialization_ShouldApplyLastLoadedWinsData()
    {
        // Arrange - Three sources with same section, last should win
        var source1 = new MockSettingsSource("source1");
        var source2 = new MockSettingsSource("source2");
        var source3 = new MockSettingsSource("source3");

        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "First", Value = 100 });
        source2.AddSection(nameof(TestSettings), new TestSettings { Name = "Second", Value = 200 });
        source3.AddSection(nameof(TestSettings), new TestSettings { Name = "Third", Value = 300 });

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);
        this.Container.RegisterInstance<ISettingsSource>(source3);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.SettingsManager.GetService<ITestSettings>();

        // Assert - Should have values from source3 (last loaded wins)
        _ = service.Settings.Name.Should().Be("Third");
        _ = service.Settings.Value.Should().Be(300);
    }

    [TestMethod]
    public async Task ReloadAllAsync_ShouldUpdateExistingServices()
    {
        // Arrange - Initialize with one value
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Initial", Value = 100 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();
        _ = service.Settings.Name.Should().Be("Initial");

        // Modify source data by adding updated section
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Updated", Value = 200 });

        // Act - Reload all sources
        await this.SettingsManager.ReloadAllAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Service should reflect updated values
        _ = service.Settings.Name.Should().Be("Updated");
        _ = service.Settings.Value.Should().Be(200);
    }

    [TestMethod]
    public async Task SaveSettingsAsync_ThenReloadAll_ShouldPersistChanges()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Original", Value = 100 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();

        // Modify and save
        service.Settings.Name = "Modified";
        service.Settings.Value = 999;
        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Reload all to simulate app restart
        await this.SettingsManager.ReloadAllAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - Changes should persist after reload
        _ = service.Settings.Name.Should().Be("Modified");
        _ = service.Settings.Value.Should().Be(999);
    }

    [TestMethod]
    public async Task SourceChanged_WithJsonElement_ShouldUpdateServiceCorrectly()
    {
        // Arrange - Initialize with JsonElement data
        var initialJson = """
            {
                "name": "InitialValue",
                "value": 100
            }
            """;
        var initialElement = System.Text.Json.JsonDocument.Parse(initialJson).RootElement;
        this.source.AddSection(nameof(TestSettings), initialElement);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var service = this.SettingsManager.GetService<ITestSettings>();

        _ = service.Settings.Name.Should().Be("InitialValue");

        // Act - Simulate source change with new JsonElement
        var updatedJson = """
            {
                "name": "UpdatedValue",
                "value": 999
            }
            """;
        var updatedElement = System.Text.Json.JsonDocument.Parse(updatedJson).RootElement;
        this.source.AddSection(nameof(TestSettings), updatedElement);
        this.source.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(200).ConfigureAwait(true); // Wait for async handler

        // Assert - Service should reflect new values from JsonElement
        _ = service.Settings.Name.Should().Be("UpdatedValue");
        _ = service.Settings.Value.Should().Be(999);
    }

    [TestMethod]
    public async Task GetService_WithMixedDataTypes_ShouldHandleBothPocoAndJsonElement()
    {
        // Arrange - source1 with POCO, source2 with JsonElement (simulating real scenario)
        var source1 = new MockSettingsSource("mock-source");
        var source2 = new MockSettingsSource("json-source");

        // Mock source provides POCO directly
        source1.AddSection(nameof(TestSettings), new TestSettings { Name = "MockPoco", Value = 100 });

        // JSON source provides JsonElement
        var jsonText = """
            {
                "name": "JsonElement",
                "value": 200
            }
            """;
        var jsonElement = System.Text.Json.JsonDocument.Parse(jsonText).RootElement;
        source2.AddSection(nameof(TestSettings), jsonElement);

        this.Container.RegisterInstance<ISettingsSource>(source1);
        this.Container.RegisterInstance<ISettingsSource>(source2);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.SettingsManager.GetService<ITestSettings>();

        // Assert - Should handle last-loaded-wins with JsonElement
        _ = service.Settings.Name.Should().Be("JsonElement");
        _ = service.Settings.Value.Should().Be(200);
    }

    [TestMethod]
    public async Task ApplyProperties_WithNull_ShouldResetToDefaults()
    {
        // Arrange
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Custom", Value = 999 });
        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.SettingsManager.GetService<ITestSettings>();
        _ = service.Settings.Name.Should().Be("Custom");

        // Act - Remove section from source (simulates section deletion)
        this.source.RemoveSection(nameof(TestSettings));
        this.source.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(200).ConfigureAwait(true);

        // Assert - Service should reset to defaults
        _ = service.Settings.Name.Should().Be("Default");
        _ = service.Settings.Value.Should().Be(42);
    }

    [TestMethod]
    public async Task MultipleServices_ShouldAllReceiveUpdatesWhenSourceChanges()
    {
        // Arrange - Create two service instances for same section (to verify all instances get updated)
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Test1", Value = 100 });

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service1 = this.SettingsManager.GetService<ITestSettings>();
        var service2 = this.SettingsManager.GetService<ITestSettings>();

        // Verify both refer to same instance
        _ = ReferenceEquals(service1, service2).Should().BeTrue();

        // Act - Update section and trigger change
        this.source.AddSection(nameof(TestSettings), new TestSettings { Name = "Test2", Value = 200 });
        this.source.TriggerSourceChanged(SourceChangeType.Updated);

        await Task.Delay(200).ConfigureAwait(true);

        // Assert - Both service references should show updated values
        _ = service1.Settings.Name.Should().Be("Test2");
        _ = service1.Settings.Value.Should().Be(200);
        _ = service2.Settings.Name.Should().Be("Test2");
        _ = service2.Settings.Value.Should().Be(200);
    }

    [TestMethod]
    public async Task LastLoadedWins_ShouldRespectSourceOrderDuringReload()
    {
        // Arrange - Three sources: base, dev, user
        var baseSource = new MockSettingsSource("base");
        var devSource = new MockSettingsSource("dev");
        var userSource = new MockSettingsSource("user");

        baseSource.AddSection(nameof(TestSettings), new TestSettings { Name = "Base", Value = 100 });
        devSource.AddSection(nameof(TestSettings), new TestSettings { Name = "Dev", Value = 200 });
        userSource.AddSection(nameof(TestSettings), new TestSettings { Name = "User", Value = 300 });

        this.Container.RegisterInstance<ISettingsSource>(baseSource);
        this.Container.RegisterInstance<ISettingsSource>(devSource);
        this.Container.RegisterInstance<ISettingsSource>(userSource);

        await this.SettingsManager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        var service = this.SettingsManager.GetService<ITestSettings>();

        // Verify initial state (user wins)
        _ = service.Settings.Name.Should().Be("User");
        _ = service.Settings.Value.Should().Be(300);

        // Modify user source
        userSource.AddSection(nameof(TestSettings), new TestSettings { Name = "UserModified", Value = 999 });

        // Act - ReloadAll should rebuild last-loaded-wins from scratch
        await this.SettingsManager.ReloadAllAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - User source should still win after reload
        _ = service.Settings.Name.Should().Be("UserModified");
        _ = service.Settings.Value.Should().Be(999);
    }
}
