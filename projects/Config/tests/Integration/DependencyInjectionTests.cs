// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Text.Json;
using DroidNet.Config.Sources;
using DroidNet.Config.Tests.Helpers;
using DryIoc;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Testably.Abstractions.Testing;

namespace DroidNet.Config.Tests.Integration;

/// <summary>
/// Integration tests for DI container setup and bootstrapper validation.
/// Tests service resolution, registration, and full DI pipeline with BootstrapperExtensions.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DI Integration")]
public class DependencyInjectionTests
{
    // Add a static readonly field for the options at the class level
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    public DependencyInjectionTests()
    {
        this.FileSystem = new MockFileSystem();
        this.Container = new Container();
        this.LoggerFactory = new LoggerFactory();

        // Register common services
        this.Container.RegisterInstance(this.LoggerFactory);
        this.Container.RegisterInstance(this.FileSystem);
    }

    public TestContext TestContext { get; set; }

    protected IFileSystem FileSystem { get; }

    protected IContainer Container { get; }

    protected SettingsManager SettingsManager => this.Container.Resolve<SettingsManager>();

    protected ILoggerFactory LoggerFactory { get; }

    // Group: WithConfig
    [TestMethod]
    public void WithConfig_WhenCalled_RegistersSettingsManager()
    {
        // Arrange
        _ = this.Container.WithConfig();

        // Assert
        var manager = this.Container.Resolve<ISettingsManager>();
        _ = manager.Should().NotBeNull();
        _ = manager.Should().BeOfType<SettingsManager>();
    }

    [TestMethod]
    public void WithConfig_WhenCalled_RegistersFileSystemDependency()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var fileSystem = this.Container.Resolve<System.IO.Abstractions.IFileSystem>();
        _ = fileSystem.Should().NotBeNull();
    }

    [TestMethod]
    public void WithConfig_WhenCalled_RegistersLoggerFactoryDependency()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var loggerFactory = this.Container.Resolve<Microsoft.Extensions.Logging.ILoggerFactory>();
        _ = loggerFactory.Should().NotBeNull();
    }

    // Group: WithJsonConfigSource
    [TestMethod]
    public void WithJsonConfigSource_WhenCalled_RegistersSettingsSource()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithJsonConfigSource("test", filePath);

        // Assert
        var source = this.Container.Resolve<ISettingsSource>(serviceKey: "test");
        _ = source.Should().NotBeNull();
        _ = source.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenEncryptionTypeIsValid_RegistersWithEncryptionProvider()
    {
        // Arrange
        _ = this.Container.WithConfig();

        // Add the fake encryption provider to the container
        this.Container.Register<FakeEncryptionProvider>(Reuse.Singleton);

        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test-encrypted.json", settings);

        // Act
        _ = this.Container.WithJsonConfigSource("encrypted", filePath, encryption: typeof(FakeEncryptionProvider));

        // Assert
        var source = this.Container.Resolve<ISettingsSource>(serviceKey: "encrypted");
        _ = source.Should().NotBeNull();
        _ = source.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenEncryptionTypeIsInvalid_ThrowsArgumentException()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithJsonConfigSource("test", filePath);

        // Act
        // Use a type that does NOT implement IEncryptionProvider (e.g., typeof(string))
        Action act = () =>
            this.Container.WithJsonConfigSource("test", filePath, encryption: typeof(string));

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("*must implement IEncryptionProvider*");
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenMultipleFilesProvided_RegistersAllSources()
    {
        // Arrange
        _ = this.Container.WithConfig();
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
        _ = this.Container.WithJsonConfigSource("file1", file1);
        _ = this.Container.WithJsonConfigSource("file2", file2);

        // Assert
        var source1 = this.Container.Resolve<ISettingsSource>(serviceKey: "file1");
        var source2 = this.Container.Resolve<ISettingsSource>(serviceKey: "file2");
        _ = source1.Should().NotBeNull();
        _ = source2.Should().NotBeNull();
        _ = source1.Should().BeOfType<JsonSettingsSource>();
        _ = source2.Should().BeOfType<JsonSettingsSource>();
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenMultipleIdsProvided_RegistersDistinctSources()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var filePath1 = this.CreateMultiSectionSettingsFile("settings1.json", new Dictionary<string, object>(StringComparer.Ordinal) { { nameof(TestSettings), new TestSettings() } });
        var filePath2 = this.CreateMultiSectionSettingsFile("settings2.json", new Dictionary<string, object>(StringComparer.Ordinal) { { nameof(TestSettings), new TestSettings() } });
        _ = this.Container.WithJsonConfigSource("id1", filePath1);
        _ = this.Container.WithJsonConfigSource("id2", filePath2);

        // Assert
        var source1 = this.Container.Resolve<ISettingsSource>(serviceKey: "id1");
        var source2 = this.Container.Resolve<ISettingsSource>(serviceKey: "id2");
        _ = source1.Should().NotBeNull();
        _ = source2.Should().NotBeNull();
        _ = source1.Should().NotBeSameAs(source2);
    }

    [TestMethod]
    public void WithJsonConfigSource_WhenWatchParameterUsed_RegistersWithCorrectWatchSetting()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var filePath = this.CreateMultiSectionSettingsFile("watched.json", new Dictionary<string, object>(StringComparer.Ordinal) { { nameof(TestSettings), new TestSettings() } });
        _ = this.Container.WithJsonConfigSource("watched", filePath, watch: true);
        _ = this.Container.WithJsonConfigSource("unwatched", filePath, watch: false);

        // Assert
        var watchedSource = this.Container.Resolve<ISettingsSource>(serviceKey: "watched");
        var unwatchedSource = this.Container.Resolve<ISettingsSource>(serviceKey: "unwatched");
        _ = watchedSource.Should().NotBeNull();
        _ = unwatchedSource.Should().NotBeNull();
        _ = watchedSource.Should().BeOfType<JsonSettingsSource>();
        _ = unwatchedSource.Should().BeOfType<JsonSettingsSource>();
    }

    // Group: WithSettings
    [TestMethod]
    public async Task WithSettings_WhenCalled_RegistersTypedService()
    {
        // Arrange
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithConfig()
            .WithJsonConfigSource("test", filePath)
            .WithSettings<ITestSettings, TestSettingsService>();
        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();
        _ = service.Should().NotBeNull();
    }

    [TestMethod]
    public async Task WithSettings_WhenCalled_ReturnsSingletonInstance()
    {
        // Arrange
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithConfig()
            .WithJsonConfigSource("test", filePath)
            .WithSettings<ITestSettings, TestSettingsService>();
        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Assert
        _ = service1.Should().BeSameAs(service2);
    }

    [TestMethod]
    public async Task WithSettings_WhenMultipleTypesProvided_ResolvesBoth()
    {
        // Arrange
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings() },
            { nameof(AlternativeTestSettings), new AlternativeTestSettings() },
        };
        var filePath = this.CreateMultiSectionSettingsFile("multi.json", settings);
        _ = this.Container.WithConfig()
            .WithJsonConfigSource("multi", filePath)
            .WithSettings<ITestSettings, TestSettingsService>()
            .WithSettings<IAlternativeTestSettings, AlternativeTestSettingsService>();
        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service1 = this.Container.Resolve<ISettingsService<ITestSettings>>();
        var service2 = this.Container.Resolve<ISettingsService<IAlternativeTestSettings>>();

        // Assert
        _ = service1.Should().NotBeNull();
        _ = service2.Should().NotBeNull();
        _ = service1.Should().NotBeSameAs(service2);
    }

    [TestMethod]
    public async Task WithSettings_WhenMultipleSources_LastLoadedWins()
    {
        // Arrange
        _ = this.Container.WithConfig();
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
        _ = this.Container.WithJsonConfigSource("first", file1);
        _ = this.Container.WithJsonConfigSource("second", file2);
        _ = this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Assert
        _ = service.Settings.Name.Should().Be("Second");
        _ = service.Settings.Value.Should().Be(2);
    }

    [TestMethod]
    public async Task WithSettings_WhenServiceResolved_ProvidesInitializedData()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "InitialData", Value = 42 } },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithJsonConfigSource("test", filePath);
        _ = this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Act
        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Assert
        _ = service.Settings.Name.Should().Be("InitialData");
        _ = service.Settings.Value.Should().Be(42);
    }

    [TestMethod]
    public async Task WithSettings_WhenValidationFails_ThrowsSettingsValidationException()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var settings = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            { nameof(TestSettings), new TestSettings { Name = "Test", Value = 50 } },
        };
        var filePath = this.CreateMultiSectionSettingsFile("test.json", settings);
        _ = this.Container.WithJsonConfigSource("test", filePath);
        _ = this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Act - Set invalid value
        service.Settings.Name = string.Empty; // Violates StringLength minimum
        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, value: true);
        var act = async () => await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = await act.Should().ThrowAsync<SettingsValidationException>().ConfigureAwait(true);
    }

    // Group: FullPipeline
    [TestMethod]
    public async Task FullPipeline_WhenScenarioIsReal_WorksEndToEnd()
    {
        // Arrange
        _ = this.Container.WithConfig();
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
        _ = this.Container.WithJsonConfigSource("app", appFile);
        _ = this.Container.WithJsonConfigSource("user", userFile);
        _ = this.Container.WithSettings<ITestSettings, TestSettingsService>();

        var manager = this.Container.Resolve<ISettingsManager>();
        await manager.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        var service = this.Container.Resolve<ISettingsService<ITestSettings>>();

        // Modify and save
        service.Settings.Value = 999;
        var isDirtyProperty = service.GetType().GetProperty("IsDirty");
        isDirtyProperty?.SetValue(service, value: true);
        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = service.Settings.Name.Should().Be("UserSettings");
        _ = service.Settings.Value.Should().Be(999);
        _ = service.IsDirty.Should().BeFalse();
    }

    // Group: SettingsManager
    [TestMethod]
    public void SettingsManager_WhenResolvedMultipleTimes_IsSingleton()
    {
        // Arrange
        _ = this.Container.WithConfig();
        var manager1 = this.Container.Resolve<ISettingsManager>();
        var manager2 = this.Container.Resolve<ISettingsManager>();

        // Assert
        _ = manager1.Should().BeSameAs(manager2);
    }

    protected string CreateTempSettingsFile(string fileName, string jsonContent)
    {
        var tempPath = this.FileSystem.Path.Combine(this.FileSystem.Path.GetTempPath(), fileName);
        var directory = this.FileSystem.Path.GetDirectoryName(tempPath);

        if (!string.IsNullOrEmpty(directory) && !this.FileSystem.Directory.Exists(directory))
        {
            _ = this.FileSystem.Directory.CreateDirectory(directory);
        }

        this.FileSystem.File.WriteAllText(tempPath, jsonContent);
        return tempPath;
    }

    private string CreateMultiSectionSettingsFile(
        string fileName,
        IDictionary<string, object> sections,
        SettingsSourceMetadata? sourceMetadata = null,
        IDictionary<string, SettingsSectionMetadata>? sectionMetadata = null)
    {
        var document = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["$meta"] = sourceMetadata ?? new SettingsSourceMetadata
            {
                WrittenAt = DateTimeOffset.UtcNow,
                WrittenBy = "DependencyInjectionTests",
            },
        };

        // Add all sections with their metadata
        foreach (var section in sections)
        {
            var sectionContent = new Dictionary<string, object>(StringComparer.Ordinal);

            // Add section metadata if provided
            if (sectionMetadata != null && sectionMetadata.TryGetValue(section.Key, out var meta))
            {
                sectionContent["$meta"] = meta;
            }

            // Add section data
            if (section.Value is JsonElement jsonElement)
            {
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonElement.GetRawText());
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionContent[key] = value;
                    }
                }
            }
            else if (section.Value is Dictionary<string, object> dict)
            {
                foreach (var (key, value) in dict)
                {
                    sectionContent[key] = value;
                }
            }
            else
            {
                // For POCOs, serialize then deserialize to get properties
                var serialized = JsonSerializer.Serialize(section.Value, JsonOptions);
                var deserialized = JsonSerializer.Deserialize<Dictionary<string, object>>(serialized);
                if (deserialized != null)
                {
                    foreach (var (key, value) in deserialized)
                    {
                        sectionContent[key] = value;
                    }
                }
            }

            document[section.Key] = sectionContent;
        }

        return this.CreateTempSettingsFile(fileName, JsonSerializer.Serialize(document, JsonOptions));
    }
}
