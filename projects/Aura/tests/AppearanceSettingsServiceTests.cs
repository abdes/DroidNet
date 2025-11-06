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

/// <summary>
/// Unit tests for the <see cref="AppearanceSettingsService"/> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
[TestCategory("AppearanceSettingsServiceTests")]
public sealed partial class AppearanceSettingsServiceTests : IDisposable
{
    private Container container = null!;
    private SettingsManager settingsManager = null!;
    private LoggerFactory loggerFactory = null!;
    private InMemorySettingsSource testSource = null!;
    private AppearanceSettingsService service = null!;
    private bool isDisposed;

    public required TestContext TestContext { get; set; }

    [TestInitialize]
    public void TestInitialize()
    {
        this.container = new Container();
        this.loggerFactory = new LoggerFactory();
        this.container.RegisterInstance(this.loggerFactory);

        this.testSource = new InMemorySettingsSource("test-source");
        this.container.RegisterInstance<ISettingsSource>(this.testSource);

        this.container.Register<SettingsManager>(Reuse.Singleton);
        this.settingsManager = this.container.Resolve<SettingsManager>();
        this.settingsManager.InitializeAsync(this.TestContext.CancellationToken).GetAwaiter().GetResult();

        this.service = new AppearanceSettingsService(this.settingsManager, this.loggerFactory);
    }

    [TestCleanup]
    public void TestCleanup() => this.service?.Dispose();

    [TestMethod]
    public void Constructor_InitializesWithDefaultValues()
    {
        // Assert
        _ = this.service.Should().NotBeNull();
        _ = this.service.AppThemeMode.Should().Be(ElementTheme.Default);
        _ = this.service.AppThemeBackgroundColor.Should().Be("#00000000");
        _ = this.service.AppThemeFontFamily.Should().Be("Segoe UI Variable");
        _ = this.service.SectionName.Should().Be(AppearanceSettings.ConfigSectionName);
    }

    [TestMethod]
    public void ApplyProperties_UpdatesAllProperties()
    {
        // Arrange
        var customSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF123456",
            AppThemeFontFamily = "Arial",
        };

        // Act
        this.service.ApplyProperties(customSettings);

        // Assert
        _ = this.service.AppThemeMode.Should().Be(ElementTheme.Dark);
        _ = this.service.AppThemeBackgroundColor.Should().Be("#FF123456");
        _ = this.service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void AppThemeMode_SetValue_UpdatesProperty()
    {
        // Act
        this.service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = this.service.AppThemeMode.Should().Be(ElementTheme.Light);
    }

    [TestMethod]
    public void AppThemeMode_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        var propertyChangedRaised = false;
        this.service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(this.service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        this.service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = propertyChangedRaised.Should().BeTrue();
    }

    [TestMethod]
    public void AppThemeMode_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        this.service.AppThemeMode = ElementTheme.Light;
        var propertyChangedRaised = false;
        this.service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(this.service.AppThemeMode), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        this.service.AppThemeMode = ElementTheme.Light;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse();
    }

    [TestMethod]
    public void AppThemeMode_SetValue_MarksDirty()
    {
        // Act
        this.service.AppThemeMode = ElementTheme.Dark;

        // Assert
        _ = this.service.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(ElementTheme.Default, DisplayName = "Default Theme")]
    [DataRow(ElementTheme.Light, DisplayName = "Light Theme")]
    [DataRow(ElementTheme.Dark, DisplayName = "Dark Theme")]
    public void AppThemeMode_SetValidValues_StoresCorrectly(ElementTheme theme)
    {
        // Act
        this.service.AppThemeMode = theme;

        // Assert
        _ = this.service.AppThemeMode.Should().Be(theme);
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_UpdatesProperty()
    {
        // Act
        this.service.AppThemeBackgroundColor = "#FFFFFFFF";

        // Assert
        _ = this.service.AppThemeBackgroundColor.Should().Be("#FFFFFFFF");
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        var propertyChangedRaised = false;
        this.service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(this.service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        this.service.AppThemeBackgroundColor = "#FF123456";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue();
    }

    [TestMethod]
    public void AppThemeBackgroundColor_SetSameValue_DoesNotRaisePropertyChangedEvent()
    {
        // Arrange
        const string testColor = "#FF123456";
        this.service.AppThemeBackgroundColor = testColor;
        var propertyChangedRaised = false;
        this.service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(this.service.AppThemeBackgroundColor), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        this.service.AppThemeBackgroundColor = testColor;

        // Assert
        _ = propertyChangedRaised.Should().BeFalse();
    }

    [TestMethod]
    [DataRow("#FFFFFF", DisplayName = "6-digit hex color")]
    [DataRow("#FF000000", DisplayName = "8-digit hex color with alpha")]
    [DataRow("#12ABCD", DisplayName = "6-digit hex with mixed case")]
    [DataRow("#AABBCCDD", DisplayName = "8-digit hex with mixed case")]
    public void AppThemeBackgroundColor_SetValidHexColor_StoresCorrectly(string color)
    {
        // Act
        this.service.AppThemeBackgroundColor = color;

        // Assert
        _ = this.service.AppThemeBackgroundColor.Should().Be(color);
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_UpdatesProperty()
    {
        // Act
        this.service.AppThemeFontFamily = "Arial";

        // Assert
        _ = this.service.AppThemeFontFamily.Should().Be("Arial");
    }

    [TestMethod]
    public void AppThemeFontFamily_SetValue_RaisesPropertyChangedEvent()
    {
        // Arrange
        var propertyChangedRaised = false;
        this.service.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(this.service.AppThemeFontFamily), StringComparison.Ordinal))
            {
                propertyChangedRaised = true;
            }
        };

        // Act
        this.service.AppThemeFontFamily = "Times New Roman";

        // Assert
        _ = propertyChangedRaised.Should().BeTrue();
    }

    [TestMethod]
    [DataRow("Arial", DisplayName = "Arial font")]
    [DataRow("Courier New", DisplayName = "Courier New font")]
    [DataRow("Times New Roman", DisplayName = "Times New Roman font")]
    [DataRow("Segoe UI", DisplayName = "Segoe UI font")]
    public void AppThemeFontFamily_SetValidFontNames_StoresCorrectly(string fontFamily)
    {
        // Act
        this.service.AppThemeFontFamily = fontFamily;

        // Assert
        _ = this.service.AppThemeFontFamily.Should().Be(fontFamily);
    }

    [TestMethod]
    public void ApplyProperties_RaisesPropertyChangedEvents()
    {
        // Arrange
        var newSettings = new AppearanceSettings
        {
            AppThemeMode = ElementTheme.Dark,
            AppThemeBackgroundColor = "#FF112233",
            AppThemeFontFamily = "Consolas",
        };

        var changedProperties = new List<string>();
        this.service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName != null)
            {
                changedProperties.Add(args.PropertyName);
            }
        };

        // Act
        this.service.ApplyProperties(newSettings);

        // Assert
        _ = changedProperties.Should().Contain(nameof(this.service.AppThemeMode));
        _ = changedProperties.Should().Contain(nameof(this.service.AppThemeBackgroundColor));
        _ = changedProperties.Should().Contain(nameof(this.service.AppThemeFontFamily));
    }

    [TestMethod]
    public void Save_WhenNotDirty_Succeeds()
    {
        // Act
        var act = async () => await this.service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = act.Should().NotThrowAsync();
    }

    [TestMethod]
    public async Task Save_WhenDirty_ClearsDirtyFlag()
    {
        // Arrange
        this.service.AppThemeMode = ElementTheme.Dark;

        // Act
        await this.service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = this.service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public void Save_WhenSourceThrows_PropagatesException()
    {
        // Arrange
        var throwingSource = new ThrowingSettingsSource("thrower");
        this.settingsManager.AddSourceAsync(throwingSource, this.TestContext.CancellationToken).GetAwaiter().GetResult();
        this.service.AppThemeMode = ElementTheme.Dark;

        // Act
        var act = async () => await this.service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = act.Should().ThrowAsync<IOException>();
    }

    [TestMethod]
    public void Dispose_CanBeCalledMultipleTimes()
    {
        // Arrange
        var tempService = new AppearanceSettingsService(this.settingsManager, this.loggerFactory);

        // Act
        tempService.Dispose();
        var act = tempService.Dispose;

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public async Task MultiplePropertyChanges_MaintainsDirtyStateCorrectly()
    {
        // Assert initial state
        _ = this.service.IsDirty.Should().BeFalse();

        // Make changes
        this.service.AppThemeMode = ElementTheme.Dark;
        _ = this.service.IsDirty.Should().BeTrue();

        this.service.AppThemeBackgroundColor = "#FF123456";
        _ = this.service.IsDirty.Should().BeTrue();

        this.service.AppThemeFontFamily = "Arial";
        _ = this.service.IsDirty.Should().BeTrue();

        // Save
        await this.service.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = this.service.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public void PropertyChangeEventSequence_FiresInOrder()
    {
        // Arrange
        var eventSequence = new List<string>();
        this.service.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName is not null && !string.Equals(args.PropertyName, nameof(this.service.IsDirty), StringComparison.Ordinal))
            {
                eventSequence.Add(args.PropertyName);
            }
        };

        // Act
        this.service.AppThemeMode = ElementTheme.Dark;
        this.service.AppThemeBackgroundColor = "#FF123456";
        this.service.AppThemeFontFamily = "Arial";

        // Assert
        _ = eventSequence.Should().Equal(
            nameof(this.service.AppThemeMode),
            nameof(this.service.AppThemeBackgroundColor),
            nameof(this.service.AppThemeFontFamily));
    }

    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.service?.Dispose();
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
