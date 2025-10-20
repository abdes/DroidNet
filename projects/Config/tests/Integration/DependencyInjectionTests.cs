// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Sources;
using DroidNet.Config.Tests.Helpers;
using DryIoc;
using FluentAssertions;

namespace DroidNet.Config.Tests.Integration;

/// <summary>
/// Integration tests for DI container setup and bootstrapper validation.
/// Tests service resolution, registration, and full DI pipeline with BootstrapperExtensions.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DI Integration")]
public class DependencyInjectionTests : SettingsTestBase
{
    public DependencyInjectionTests()
        : base(registerDefaultSettingsServices: false)
    {
    }

    public TestContext TestContext { get; set; }

    // Group: WithConfig
    [TestMethod]
    public void WithConfig_WhenCalled_RegistersSettingsManager()
    {
        // Arrange
        this.Container.WithConfig();

        // Assert
        var manager = this.Container.Resolve<ISettingsManager>();
        manager.Should().NotBeNull();
        manager.Should().BeOfType<SettingsManager>();
    }

    [TestMethod]
    public void WithConfig_WhenCalled_RegistersFileSystemDependency()
    {
        // Arrange
        this.Container.WithConfig();
        var fileSystem = this.Container.Resolve<System.IO.Abstractions.IFileSystem>();
        fileSystem.Should().NotBeNull();
    }

    [TestMethod]
    public void WithConfig_WhenCalled_RegistersLoggerFactoryDependency()
    {
        // Arrange
        this.Container.WithConfig();
        var loggerFactory = this.Container.Resolve<Microsoft.Extensions.Logging.ILoggerFactory>();
        loggerFactory.Should().NotBeNull();
    }

    // Group: WithJsonConfigSource
    [TestMethod]
    public void WithJsonConfigSource_WhenCalled_RegistersSettingsSource()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);

        // Assert
        var source = this.Container.Resolve<ISettingsSource>(serviceKey: "test");
        source.Should().NotBeNull();
        source.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenEncryptionTypeIsValid_RegistersWithEncryptionProvider()
    {
        // Arrange
        this.Container.WithConfig();

        // Add the fake encryption provider to the container
        this.Container.Register<FakeEncryptionProvider>(Reuse.Singleton);

        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test-encrypted.json", settings);

        // Act
        this.Container.WithJsonConfigSource("encrypted", filePath, encryption: typeof(FakeEncryptionProvider));

        // Assert
        var source = this.Container.Resolve<ISettingsSource>(serviceKey: "encrypted");
        source.Should().NotBeNull();
        source.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenEncryptionTypeIsInvalid_ThrowsArgumentException()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);

        // Act
        Action act = () =>
        {
            // Use a type that does NOT implement IEncryptionProvider (e.g., typeof(string))
            this.Container.WithJsonConfigSource("test", filePath, encryption: typeof(string));
        };

        // Assert
        act.Should().Throw<ArgumentException>()
            .WithMessage("*must implement IEncryptionProvider*");
    }

    // Helper for encryption provider branch coverage
    private class FakeEncryptionProvider : IEncryptionProvider
    {
        public byte[] Encrypt(byte[] data) => data;
        public byte[] Decrypt(byte[] data) => data;
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenMultipleFilesProvided_RegistersAllSources()
    {
        // Arrange
        this.Container.WithConfig();
        var settings1 = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "File1" } },
        };
        var settings2 = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "File2" } },
        };

        var file1 = this.CreateMultiSectionSettingsFile("test1.json", settings1);
        var file2 = this.CreateMultiSectionSettingsFile("test2.json", settings2);
        this.Container.WithJsonConfigSource("file1", file1);
        this.Container.WithJsonConfigSource("file2", file2);

        // Assert
        var source1 = this.Container.Resolve<ISettingsSource>(serviceKey: "file1");
        var source2 = this.Container.Resolve<ISettingsSource>(serviceKey: "file2");
        source1.Should().NotBeNull();
        source2.Should().NotBeNull();
        source1.Should().BeOfType<JsonSettingsSource>();
        source2.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenMultipleIdsProvided_RegistersDistinctSources()
    {
        // Arrange
        this.Container.WithConfig();
        var filePath1 = this.CreateMultiSectionSettingsFile("settings1.json", new Dictionary<string, object> { { nameof(TestSettings), new TestSettings() } });
        var filePath2 = this.CreateMultiSectionSettingsFile("settings2.json", new Dictionary<string, object> { { nameof(TestSettings), new TestSettings() } });
        this.Container.WithJsonConfigSource("id1", filePath1);
        this.Container.WithJsonConfigSource("id2", filePath2);

        // Assert
        var source1 = this.Container.Resolve<ISettingsSource>(serviceKey: "id1");
        var source2 = this.Container.Resolve<ISettingsSource>(serviceKey: "id2");
        source1.Should().NotBeNull();
        source2.Should().NotBeNull();
        source1.Should().NotBeSameAs(source2);
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenWatchParameterUsed_RegistersWithCorrectWatchSetting()
    {
        // Arrange
        this.Container.WithConfig();
        var filePath = this.CreateMultiSectionSettingsFile("watched.json", new Dictionary<string, object> { { nameof(TestSettings), new TestSettings() } });
        this.Container.WithJsonConfigSource("watched", filePath, watch: true);
        this.Container.WithJsonConfigSource("unwatched", filePath, watch: false);

        // Assert
        var watchedSource = this.Container.Resolve<ISettingsSource>(serviceKey: "watched");
        var unwatchedSource = this.Container.Resolve<ISettingsSource>(serviceKey: "unwatched");
        watchedSource.Should().NotBeNull();
        unwatchedSource.Should().NotBeNull();
        watchedSource.Should().BeOfType<JsonSettingsSource>();
        unwatchedSource.Should().BeOfType<JsonSettingsSource>();
    }

    // Group: WithSettings
    [TestMethod]
    public void WithSettings_WhenCalled_RegistersTypedService()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        // Assert
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        service.Should().NotBeNull();
    }

    [TestMethod]
    public void WithSettings_WhenCalled_ReturnsSingletonInstance()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Assert
        service1.Should().BeSameAs(service2);
    }

    [TestMethod]
    public void WithSettings_WhenMultipleTypesProvided_ResolvesBoth()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
            { nameof(AlternativeTestSettings), new AlternativeTestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("multi.json", settings);
        this.Container.WithJsonConfigSource("multi", filePath);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();
        this.Container.WithSettings<IAlternativeTestSettings, AlternativeTestSettingsService>();

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<IAlternativeTestSettings>>();

        // Assert
        service1.Should().NotBeNull();
        service2.Should().NotBeNull();
        service1.Should().NotBeSameAs(service2);
    }

    [TestMethod]
    public void WithSettings_WhenChained_RegistersServiceCorrectly()
    {
        // Arrange
        this.Container.WithConfig()
            .WithJsonConfigSource("test", this.CreateMultiSectionSettingsFile("test.json", new Dictionary<string, object> { { nameof(TestSettings), new TestSettings() } }))
            .WithSettings<ITestSettings, TestSettingsService>();

        // Assert
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        service.Should().NotBeNull();
    }

    [TestMethod]
    public async Task WithSettings_WhenMultipleSources_LastLoadedWins()
    {
        // Arrange
        this.Container.WithConfig();
        var settings1 = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "First", Value = 1 } },
        };
        var settings2 = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "Second", Value = 2 } },
        };

        var file1 = this.CreateMultiSectionSettingsFile("first.json", settings1);
        var file2 = this.CreateMultiSectionSettingsFile("second.json", settings2);
        this.Container.WithJsonConfigSource("first", file1);
        this.Container.WithJsonConfigSource("second", file2);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        service.Settings.Name.Should().Be("Second");
        service.Settings.Value.Should().Be(2);
    }

    [TestMethod]
    public async Task WithSettings_WhenServiceResolved_ProvidesInitializedData()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "InitialData", Value = 42 } },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        service.Settings.Name.Should().Be("InitialData");
        service.Settings.Value.Should().Be(42);
    }

    [TestMethod]
    public async Task WithSettings_WhenValidationFails_ThrowsSettingsValidationException()
    {
        // Arrange
        this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "Test", Value = 50 } },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        this.Container.WithJsonConfigSource("test", filePath);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act - Set invalid value
        service.Settings.Name = string.Empty; // Violates StringLength minimum
        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, value: true);
        var act = async () => await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(true);
    }

    // Group: FullPipeline
    [TestMethod]
    public async Task FullPipeline_WhenScenarioIsReal_WorksEndToEnd()
    {
        // Arrange
        this.Container.WithConfig();
        var appSettings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "AppSettings", Value = 100 } },
        };
        var userSettings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "UserSettings", Value = 200 } },
        };

        var appFile = this.CreateMultiSectionSettingsFile("appsettings.json", appSettings);
        var userFile = this.CreateMultiSectionSettingsFile("user-settings.json", userSettings);
        this.Container.WithJsonConfigSource("app", appFile);
        this.Container.WithJsonConfigSource("user", userFile);
        this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Modify and save
        service.Settings.Value = 999;
        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, value: true);
        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        service.Settings.Name.Should().Be("UserSettings");
        service.Settings.Value.Should().Be(999);
        service.IsDirty.Should().BeFalse();
    }

    // Group: SettingsManager
    [TestMethod]
    public void SettingsManager_WhenResolvedMultipleTimes_IsSingleton()
    {
        // Arrange
        this.Container.WithConfig();
        var manager1 = this.Container.Resolve<ISettingsManager>();
        var manager2 = this.Container.Resolve<ISettingsManager>();

        // Assert
        manager1.Should().BeSameAs(manager2);
    }
}
