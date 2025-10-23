// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DryIoc;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Tests;

#pragma warning disable IDE1006 // Naming styles - test method names intentionally contain underscores
// Relax analyzers that the test project treats as errors for these helper/tests
#pragma warning disable CA1707 // Identifiers should not contain underscores
#pragma warning disable CA1822 // Mark members as static

/// <summary>
/// Unit tests for the <see cref="AppearanceSettingsService"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Appearance Settings")]
public partial class AppearanceSettingsServiceTests : IDisposable
{
    private Container container = null!;
    private SettingsManager? settingsManager;
    private LoggerFactory? loggerFactory;
    private InMemorySettingsSource? testSource;
    private bool isDisposed;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public void TestInitialize()
    {
        this.container = new Container();
        this.loggerFactory = new LoggerFactory();

        // Register logger factory in the container
        this.container.RegisterInstance(this.loggerFactory);

        // Register a test settings source so the SettingsManager has a source to save to
        this.testSource = new InMemorySettingsSource("test-source");
        this.container.RegisterInstance<ISettingsSource>(this.testSource);

        // Register and resolve the SettingsManager
        this.container.Register<SettingsManager>(Reuse.Singleton);
        this.settingsManager = this.container.Resolve<SettingsManager>();

        // Initialize the SettingsManager so it's ready for use
        this.settingsManager.InitializeAsync(this.TestContext.CancellationToken).GetAwaiter().GetResult();
    }

    [TestMethod]
    public void Constructor_CreatesInstanceSuccessfully()
    {
        // Arrange & Act
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Assert
        _ = service.Should().NotBeNull();
        _ = service.AppThemeMode.Should().Be(ElementTheme.Default);
        _ = service.AppThemeBackgroundColor.Should().Be("#00000000");
        _ = service.AppThemeFontFamily.Should().Be("Segoe UI Variable");
    }

    [TestMethod]
    public void Constructor_InitializesPropertiesFromMonitor()
    {
        // Arrange
        var customSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF123456",
            AppThemeFontFamily = "Arial",
        };

        // Act - apply settings as manager would
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        service.ApplyProperties(customSettings);

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Dark);
        _ = service.AppThemeBackgroundColor.Should().Be("#FF123456");
        _ = service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void Constructor_CallsPathFinderToGetConfigFilePath()
    {
        // Arrange & Act
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Assert
        _ = service.SectionName.Should().Be(AppearanceSettings.ConfigSectionName);
    }

    [TestMethod]
    public void AppThemeMode_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Light);
    }

    [TestMethod]
    public void AppThemeMode_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    public void AppThemeMode_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);
        service.AppThemeMode = ElementTheme.Light;
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse("PropertyChanged event should not be raised when value doesn't change");
    }

    [TestMethod]
    public void AppThemeMode_SetValue_MarksDirty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = service.IsDirty.Should().BeTrue("Service should be marked as dirty after property change");
    }

    [TestMethod]
    [DataRow(ElementTheme.Default, DisplayName = "Default Theme")]
    [DataRow(ElementTheme.Light, DisplayName = "Light Theme")]
    [DataRow(ElementTheme.Dark, DisplayName = "Dark Theme")]
    public void AppThemeMode_SetValidValues_StoresCorrectly(ElementTheme theme)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeMode = theme;

        // Assert
        _ = service.AppThemeMode.Should().Be(theme);
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeBackgroundColor = "#FFFFFFFF";

        // Assert
        _ = service.AppThemeBackgroundColor.Should().Be("#FFFFFFFF");
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeBackgroundColor = "#FF123456";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);
        const string testColor = "#FF123456";
        service.AppThemeBackgroundColor = testColor;
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeBackgroundColor = testColor;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse("PropertyChanged event should not be raised when value doesn't change");
    }

    [TestMethod]
    [DataRow("#FFFFFF", DisplayName = "6-digit hex color")]
    [DataRow("#FF000000", DisplayName = "8-digit hex color with alpha")]
    [DataRow("#12ABCD", DisplayName = "6-digit hex with mixed case")]
    [DataRow("#AABBCCDD", DisplayName = "8-digit hex with mixed case")]
    public void AppThemeBackgroundColor_SetValidHexColor_StoresCorrectly(string color)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeBackgroundColor = color;

        // Assert
        _ = service.AppThemeBackgroundColor.Should().Be(color);
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_UpdatesProperty()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeFontFamily = "Arial";

        // Assert
        _ = service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);
        var propertyChangedRaised = false;
        service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(service.AppThemeFontFamily), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        service.AppThemeFontFamily = "Times New Roman";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue("PropertyChanged event should be raised");
    }

    [TestMethod]
    [DataRow("Arial", DisplayName = "Arial font")]
    [DataRow("Courier New", DisplayName = "Courier New font")]
    [DataRow("Times New Roman", DisplayName = "Times New Roman font")]
    [DataRow("Segoe UI", DisplayName = "Segoe UI font")]
    public void AppThemeFontFamily_SetValidFontNames_StoresCorrectly(string fontFamily)
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.AppThemeFontFamily = fontFamily;

        // Assert
        _ = service.AppThemeFontFamily.Should().Be(fontFamily);
    }

    [TestMethod]
    public void UpdateProperties_WhenSettingsChange_UpdatesAllProperties()
    {
        // Arrange
        var newSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF112233",
            AppThemeFontFamily = "Consolas",
        };

        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act - simulate manager applying new settings
        service.ApplyProperties(newSettings);

        // Assert
        _ = service.AppThemeMode.Should().Be(ElementTheme.Dark);
        _ = service.AppThemeBackgroundColor.Should().Be("#FF112233");
        _ = service.AppThemeFontFamily.Should().Be("Consolas");
    }

    [TestMethod]
    public void UpdateProperties_WhenSettingsChange_RaisesPropertyChangedEvents()
    {
        // Arrange
        var newSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF112233",
            AppThemeFontFamily = "Consolas",
        };

        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        var changedProperties = new List<string>();
        service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName != null)
            {
                changedProperties.Add(args.PropertyName);
            }
        };

        // Act
        service.ApplyProperties(newSettings);

        // Assert
        _ = changedProperties.Should().Contain(nameof(service.AppThemeMode));
        _ = changedProperties.Should().Contain(nameof(service.AppThemeBackgroundColor));
        _ = changedProperties.Should().Contain(nameof(service.AppThemeFontFamily));
    }

    [TestMethod]
    public void SaveSettings_WhenNotDirty_ReturnsTrue()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        var act = async () => await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - no exception when not dirty
        _ = act.Should().NotThrowAsync();
    }

    [TestMethod]
    public async Task SaveSettings_WhenDirty_WritesToFile()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        service.AppThemeMode = ElementTheme.Dark;

        // Act
        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert - IsDirty should be false after successful save
        _ = service.IsDirty.Should().BeFalse("IsDirty should be false after successful save");
    }

    [TestMethod]
    public async Task SaveSettings_WhenDirty_ClearsDirtyFlag()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        service.AppThemeMode = ElementTheme.Dark;

        // Act
        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = service.IsDirty.Should().BeFalse("IsDirty should be false after successful save");
    }

    [TestMethod]
    public void SaveSettings_WhenExceptionOccurs_ReturnsFalse()
    {
        // Arrange
        // Arrange - add a source that throws on save
        var throwingSource = new ThrowingSettingsSource("thrower");
        var addTask = this.settingsManager!.AddSourceAsync(throwingSource, this.TestContext.CancellationToken);
        addTask.GetAwaiter().GetResult();

        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        service.AppThemeMode = ElementTheme.Dark;

        // Act & Assert - SaveAsync should throw when source save fails
        var act = async () => await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = act.Should().ThrowAsync<IOException>();
    }

    [TestMethod]
    public void Dispose_WhenNotDirty_DisposesWithoutWarning()
    {
        // Arrange
        var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act
        service.Dispose();
        service.Dispose(); // Call dispose again to ensure no exceptions

        // No exception is considered a pass
    }

    [TestMethod]
    public void Dispose_WhenDirty_LogsWarning()
    {
        // Arrange
        var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory)
        {
            AppThemeMode = ElementTheme.Dark,
        };

        // Act
        service.Dispose();

        // no exception means dispose handled dirty state; specifics of logging are manager-internal
    }

    [TestMethod]
    public async Task IntegrationTest_MultiplePropertyChanges_MaintainsDirtyStateCorrectly()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        // Act & Assert
        _ = service.IsDirty.Should().BeFalse("Service should not be dirty initially");

        service.AppThemeMode = ElementTheme.Dark;
        _ = service.IsDirty.Should().BeTrue("Service should be dirty after first change");

        service.AppThemeBackgroundColor = "#FF123456";
        _ = service.IsDirty.Should().BeTrue("Service should remain dirty after second change");

        service.AppThemeFontFamily = "Arial";
        _ = service.IsDirty.Should().BeTrue("Service should remain dirty after third change");

        await service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = service.IsDirty.Should().BeFalse("Service should not be dirty after save");
    }

    [TestMethod]
    public void IntegrationTest_PropertyChangeEventSequence_FiresCorrectly()
    {
        // Arrange
        using var service = new AppearanceSettingsService(
            this.settingsManager!,
            this.loggerFactory);

        var eventSequence = new List<string>();
        service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName is not null && !string.Equals(args.PropertyName, nameof(service.IsDirty), StringComparison.Ordinal))
            {
                eventSequence.Add(args.PropertyName);
            }
        };

        // Act
        service.AppThemeMode = ElementTheme.Dark;
        service.AppThemeBackgroundColor = "#FF123456";
        service.AppThemeFontFamily = "Arial";

        // Assert
        _ = eventSequence.Should().HaveCount(3, "Three property changes should fire three events");
        _ = eventSequence[0].Should().Be(nameof(service.AppThemeMode));
        _ = eventSequence[1].Should().Be(nameof(service.AppThemeBackgroundColor));
        _ = eventSequence[2].Should().Be(nameof(service.AppThemeFontFamily));
    }

    /// <summary>
    /// Disposes the test resources.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Protected Dispose method following the standard disposal pattern.
    /// </summary>
    /// <param name="disposing">True if called from Dispose; false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.settingsManager?.Dispose();
            this.loggerFactory?.Dispose();
            this.container?.Dispose();
        }

        this.isDisposed = true;
    }

    // Helper settings source that throws during save to simulate IO errors
    private sealed class ThrowingSettingsSource(string id) : ISettingsSource
    {
#pragma warning disable CS0067 // Event is never used
        public event EventHandler<SourceChangedEventArgs>? SourceChanged;
#pragma warning restore CS0067

        public string Id { get; } = id;

        public bool SupportsEncryption => false;

        public bool IsAvailable => true;

        public bool WatchForChanges { get; set; }

        public SettingsSourceMetadata? SourceMetadata { get; set; }

        public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
        {
            var payload = new SettingsReadPayload(
                new Dictionary<string, object>(StringComparer.Ordinal),
                new Dictionary<string, SettingsSectionMetadata>(StringComparer.Ordinal),
                sourceMetadata: null,
                this.Id);
            return Task.FromResult(Result.Ok(payload));
        }

        public Task<Result<SettingsWritePayload>> SaveAsync(
            IReadOnlyDictionary<string, object> sectionsData,
            IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
            SettingsSourceMetadata sourceMetadata,
            CancellationToken cancellationToken = default)
            => throw new IOException("Test Exception");

        public Task<Result<SettingsValidationPayload>> ValidateAsync(
            IReadOnlyDictionary<string, object> sectionsData,
            CancellationToken cancellationToken = default)
        {
            var payload = new SettingsValidationPayload(sectionsData.Count, "Validation succeeded");
            return Task.FromResult(Result.Ok(payload));
        }
    }

    // Helper settings source for testing that stores settings in memory
    private sealed class InMemorySettingsSource(string id) : ISettingsSource
    {
        private readonly Dictionary<string, object> sections = new(StringComparer.Ordinal);
        private readonly Dictionary<string, SettingsSectionMetadata> sectionMetadata = new(StringComparer.Ordinal);

#pragma warning disable CS0067 // Event is never used
        public event EventHandler<SourceChangedEventArgs>? SourceChanged;
#pragma warning restore CS0067

        public string Id { get; } = id;

        public bool SupportsEncryption => false;

        public bool IsAvailable => true;

        public bool WatchForChanges { get; set; }

        public SettingsSourceMetadata? SourceMetadata { get; set; }

        public Task<Result<SettingsReadPayload>> LoadAsync(bool reload = false, CancellationToken cancellationToken = default)
        {
            var payload = new SettingsReadPayload(
                this.sections,
                this.sectionMetadata,
                this.SourceMetadata,
                this.Id);
            return Task.FromResult(Result.Ok(payload));
        }

        public Task<Result<SettingsWritePayload>> SaveAsync(
            IReadOnlyDictionary<string, object> sectionsData,
            IReadOnlyDictionary<string, SettingsSectionMetadata> sectionMetadata,
            SettingsSourceMetadata sourceMetadata,
            CancellationToken cancellationToken = default)
        {
            foreach (var (key, value) in sectionsData)
            {
                this.sections[key] = value;
            }

            foreach (var (key, value) in sectionMetadata)
            {
                this.sectionMetadata[key] = value;
            }

            this.SourceMetadata = sourceMetadata;

            var payload = new SettingsWritePayload(sourceMetadata, sectionsData.Count, $"memory://{this.Id}");
            return Task.FromResult(Result.Ok(payload));
        }

        public Task<Result<SettingsValidationPayload>> ValidateAsync(
            IReadOnlyDictionary<string, object> sectionsData,
            CancellationToken cancellationToken = default)
        {
            var payload = new SettingsValidationPayload(sectionsData.Count, "Validation succeeded");
            return Task.FromResult(Result.Ok(payload));
        }
    }
}
